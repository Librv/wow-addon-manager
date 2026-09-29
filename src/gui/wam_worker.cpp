#include "gui/wam_worker.hpp"
#include "core/addon_installer.hpp"
#include "core/http_client.hpp"
#include "core/scoped_temp_file.hpp"
#include "core/reconciler.hpp"
#include "gui/toc_text.hpp"
#include <QList>
#include <algorithm>
#include <filesystem>
#include <map>
#include <unordered_set>

namespace wam::gui {

namespace {

ReleaseChannel parseChannel(const QString& s) {
    if (s == "beta") return ReleaseChannel::Beta;
    if (s == "alpha") return ReleaseChannel::Alpha;
    return ReleaseChannel::Release;
}

template <typename T, typename U>
QList<T> toQList(const std::vector<U>& in) {
    QList<T> out;
    out.reserve(static_cast<int>(in.size()));
    for (const auto& item : in) out.push_back(item);
    return out;
}

std::optional<int64_t> optId(qint64 id) {
    return id != 0 ? std::optional<int64_t>(id) : std::nullopt;
}

std::string flavorNameFor(CurseForgeClient& c, qint64 id) {
    if (id == 0) return {};
    try {
        for (const auto& t : c.listGameVersionTypes())
            if (t.id == id) return t.name;
    } catch (const std::exception&) {}
    return {};
}

} // namespace

WamWorker::WamWorker(QObject* parent) : QObject(parent) {}

CurseForgeClient* WamWorker::requireClient(const char* context) {
    if (!client_) {
        emit errorOccurred(context, "No CurseForge API key configured.");
        return nullptr;
    }
    return &*client_;
}

void WamWorker::emitConfig() {
    emit configChanged(config_.curseforge_api_key.has_value(),
                       config_.wow_path.has_value(),
                       QString::fromStdString(config_.wow_path.value_or("")),
                       static_cast<qint64>(config_.wow_flavor_id.value_or(0)),
                       QString::fromStdString(config_.wow_flavor_name.value_or("")));
}

void WamWorker::detectFlavor() {
    if (!client_ || !config_.wow_path.has_value() || config_.wow_flavor_id.has_value()) return;
    try {
        const auto types = client_->listGameVersionTypes();
        const auto id = CurseForgeClient::matchFlavorForFolder(types, config_.wowFolderName());
        if (!id.has_value()) return; // unknown folder name: the user picks one in Settings
        config_.wow_flavor_id = *id;
        for (const auto& t : types) if (t.id == *id) config_.wow_flavor_name = t.name;
        config_.save();
        emitConfig();
    } catch (const std::exception&) {} // no network: try again next start or when the path changes
}

void WamWorker::initialize() {
    try {
        config_ = Config::load();
        state_ = StateStore::load();
    } catch (const std::exception& e) {
        emit errorOccurred("initialize", QString::fromStdString(e.what()));
    }
    if (config_.curseforge_api_key.has_value())
        client_.emplace(*config_.curseforge_api_key);

    // A previous run killed mid-update leaves the old folders in .wam-backup.
    if (config_.wow_path.has_value()) {
        try {
            auto dir = config_.addonsDir();
            if (std::filesystem::exists(dir)) {
                auto restored = AddonInstaller::recoverInterrupted(dir);
                if (restored > 0)
                    emit errorOccurred("recovery", QString("Restored %1 addon folder(s) from an interrupted update.")
                                                       .arg(restored));
            }
        } catch (const std::exception& e) {
            emit errorOccurred("recovery", QString::fromStdString(e.what()));
        }
    }

    emitConfig();
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    detectFlavor();
}

void WamWorker::setApiKey(const QString& key) {
    config_.curseforge_api_key = key.toStdString();
    config_.save();
    client_.emplace(*config_.curseforge_api_key);
    emitConfig();
    detectFlavor();
}

void WamWorker::setWowPath(const QString& path) {
    config_.wow_path = path.toStdString();
    config_.wow_flavor_id.reset(); // it described the previous folder
    config_.wow_flavor_name.reset();
    config_.save();
    emitConfig();
    detectFlavor();
}

void WamWorker::setWowFlavor(qint64 flavorTypeId) {
    if (flavorTypeId == 0) {
        config_.wow_flavor_id.reset();
        config_.wow_flavor_name.reset();
    } else {
        config_.wow_flavor_id = flavorTypeId;
        config_.wow_flavor_name = client_ ? flavorNameFor(*client_, flavorTypeId) : std::string();
    }
    config_.save();
    emitConfig();
}

void WamWorker::search(const QString& query) {
    auto* client = requireClient("search");
    if (!client) return;
    try {
        auto results = client->search(query.toStdString());
        emit searchFinished(query, toQList<wam::CurseForgeMod>(results));
    } catch (const std::exception& e) {
        emit errorOccurred("search", QString::fromStdString(e.what()));
    }
}

void WamWorker::listGameVersionTypes() {
    auto* client = requireClient("listGameVersionTypes");
    if (!client) return;
    try {
        emit gameVersionTypesLoaded(toQList<wam::GameVersionType>(client->listGameVersionTypes()));
    } catch (const std::exception& e) {
        emit errorOccurred("listGameVersionTypes", QString::fromStdString(e.what()));
    }
}

void WamWorker::getFiles(qint64 modId, qint64 flavorTypeId) {
    auto* client = requireClient("getFiles");
    if (!client) return;
    try {
        auto files = client->getFiles(modId, optId(flavorTypeId));
        emit filesLoaded(modId, toQList<wam::CurseForgeFile>(files));
    } catch (const std::exception& e) {
        emit errorOccurred("getFiles", QString::fromStdString(e.what()));
    }
}

void WamWorker::loadInstallFiles(qint64 modId, qint64 flavorTypeId, int index) {
    if (!client_) {
        emit installFilesFailed(modId, flavorTypeId, "No CurseForge API key configured.");
        return;
    }
    try {
        auto page = client_->getFilesPage(modId, optId(flavorTypeId), index, 50);
        emit installFilesLoaded(modId, flavorTypeId, page.index, toQList<wam::CurseForgeFile>(page.files),
                                page.totalCount);
    } catch (const std::exception& e) {
        emit installFilesFailed(modId, flavorTypeId, QString::fromStdString(e.what()));
    }
}

void WamWorker::installFile(qint64 modId, qint64 fileId, qint64 flavorTypeId) {
    auto* client = requireClient("install");
    if (!client) return;
    if (!config_.wow_path.has_value()) {
        emit errorOccurred("install", "No WoW installation path configured.");
        return;
    }

    try {
        auto mod = client->getMod(modId);
        auto file = client->getFile(modId, fileId); // fresh: the download url is per request

        if (file.isBlocked()) {
            emit downloadBlocked(modId, QString::fromStdString(mod.name),
                                 QString::fromStdString(mod.slug),
                                 file.id, QString::fromStdString(file.fileName));
            return;
        }

        auto addonsDir = config_.addonsDir();
        ScopedTempFile tmpZip(Config::dataDir() / "tmp" /
                              (std::to_string(file.id) + "_" + file.fileName));
        auto dl = HttpClient::downloadToFile(*file.downloadUrl, tmpZip.path());
        if (!dl.ok()) {
            emit errorOccurred("install", "Download failed (HTTP " + QString::number(dl.status) + ").");
            return;
        }

        // Reinstalling a tracked mod replaces its old folders atomically.
        auto existing = state_.find(modId);
        auto folders = AddonInstaller::installZip(tmpZip.path(), addonsDir,
                                                  existing ? existing->folders : std::vector<std::string>{});

        InstalledAddon rec;
        rec.modId = modId;
        rec.fileId = file.id;
        rec.displayName = mod.name;
        rec.fileName = file.fileName;
        rec.channel = file.releaseType;
        rec.gameVersions = file.gameVersions;
        rec.folders = folders;
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        rec.iconUrl = !mod.logoUrl.empty() ? mod.logoUrl : (existing ? existing->iconUrl : std::string());
        if (flavorTypeId != 0) {
            rec.flavorTypeId = flavorTypeId;
            rec.flavorName = flavorNameFor(*client, flavorTypeId);
        } else if (existing.has_value()) {
            rec.flavorTypeId = existing->flavorTypeId;
            rec.flavorName = existing->flavorName;
        }

        state_.upsert(rec);
        state_.save();

        emit installFinished(rec);
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        emit errorOccurred("install", QString::fromStdString(e.what()));
    }
}

void WamWorker::installManual(qint64 modId, qint64 fileId, const QString& zipPath, qint64 flavorTypeId) {
    if (!config_.wow_path.has_value()) {
        emit errorOccurred("installManual", "No WoW installation path configured.");
        return;
    }
    const std::filesystem::path zip = zipPath.toStdString();
    if (!std::filesystem::exists(zip)) {
        emit errorOccurred("installManual", "File not found: " + zipPath);
        return;
    }

    try {
        auto existing = state_.find(modId);
        InstalledAddon rec = existing.value_or(InstalledAddon{});
        rec.modId = modId;
        rec.fileId = fileId;
        rec.displayName = existing ? existing->displayName : "mod " + std::to_string(modId);
        rec.fileName = zip.filename().string();
        rec.channel = existing ? existing->channel : ReleaseChannel::Release;
        rec.gameVersions.clear();

        if (client_) { // best-effort metadata, same as the CLI; works offline without it
            try {
                auto mod = client_->getMod(modId);
                auto file = client_->getFile(modId, fileId);
                rec.displayName = mod.name;
                rec.fileName = file.fileName;
                rec.channel = file.releaseType;
                rec.gameVersions = file.gameVersions;
                if (!mod.logoUrl.empty()) rec.iconUrl = mod.logoUrl;
            } catch (const std::exception&) {}
        }
        if (flavorTypeId != 0) {
            rec.flavorTypeId = flavorTypeId;
            rec.flavorName = client_ ? flavorNameFor(*client_, flavorTypeId) : std::string();
        }

        rec.folders = AddonInstaller::installZip(zip, config_.addonsDir(),
                                                 existing ? existing->folders : std::vector<std::string>{});
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = true;

        state_.upsert(rec);
        state_.save();
        emit installFinished(rec);
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        emit errorOccurred("installManual", QString::fromStdString(e.what()));
    }
}

void WamWorker::checkUpdate(qint64 modId, const QString& channel) {
    auto existing = state_.find(modId);
    if (!existing.has_value()) {
        emit errorOccurred("checkUpdate", "Mod " + QString::number(modId) + " is not tracked.");
        return;
    }
    auto* client = requireClient("checkUpdate");
    if (!client) return;

    try {
        auto mod = client->getMod(modId);
        const QString name = QString::fromStdString(mod.name);
        ReleaseChannel ch = channel.isEmpty() ? existing->channel : parseChannel(channel);

        // Stay within the flavor this addon was installed for (0 = unknown, unfiltered).
        auto files = client->getFiles(modId, optId(existing->flavorTypeId));
        auto chosen = CurseForgeClient::selectBestFile(files, ch);
        if (!chosen.has_value() || chosen->id == existing->fileId) {
            emit upToDate(modId, name);
            return;
        }

        // Only what we know locally (id 0 = adopted, never pinned): we
        // deliberately don't fetch it, since an adopted addon has no real
        // file id yet.
        CurseForgeFile current;
        current.id = existing->fileId;
        current.modId = modId;
        current.fileName = existing->fileName;
        current.releaseType = existing->channel;
        current.gameVersions = existing->gameVersions;

        const std::string icon = !mod.logoUrl.empty() ? mod.logoUrl : existing->iconUrl;
        emit updateAvailable(modId, name, QString::fromStdString(mod.slug), QString::fromStdString(icon),
                             QString::fromStdString(existing->flavorName), current, *chosen);
    } catch (const std::exception& e) {
        emit errorOccurred("checkUpdate", QString::fromStdString(e.what()));
    }
}

void WamWorker::checkAllUpdates() {
    if (!client_ && !state_.all().empty()) {
        emit errorOccurred("checkUpdate", "No CurseForge API key configured.");
        emit allUpdatesChecked();
        return;
    }
    for (const auto& addon : state_.all()) {
        if (addon.modId == 0) continue; // not CurseForge-sourced
        checkUpdate(addon.modId, QString()); // empty channel = each addon's own tracked channel
    }
    emit allUpdatesChecked();
}

void WamWorker::applyUpdate(qint64 modId, qint64 fileId) {
    auto fail = [&](const QString& msg) {
        emit errorOccurred("applyUpdate", msg);
        emit updateFailed(modId, msg);
    };

    auto existing = state_.find(modId);
    if (!existing) return fail("Mod " + QString::number(modId) + " is not tracked.");
    if (!client_) return fail("No CurseForge API key configured.");
    if (!config_.wow_path) return fail("No WoW installation path configured.");

    try {
        auto file = client_->getFile(modId, fileId); // re-fetch: the files list can move between check and apply
        if (file.isBlocked()) {
            auto mod = client_->getMod(modId);
            emit downloadBlocked(modId, QString::fromStdString(mod.name), QString::fromStdString(mod.slug),
                                 file.id, QString::fromStdString(file.fileName));
            return fail("Third-party downloads are blocked by the author.");
        }

        auto addonsDir = config_.addonsDir();
        ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(file.id) + "_" + file.fileName));
        auto dl = HttpClient::downloadToFile(*file.downloadUrl, tmpZip.path());
        if (!dl.ok()) return fail("Download failed (HTTP " + QString::number(dl.status) + ").");

        // Stage, then swap with rollback: a failure anywhere leaves the
        // installed version exactly as it was.
        auto folders = AddonInstaller::installZip(tmpZip.path(), addonsDir, existing->folders);

        InstalledAddon rec = *existing; // keeps flavorTypeId/flavorName
        rec.fileId = file.id;
        rec.fileName = file.fileName;
        rec.channel = file.releaseType;
        rec.gameVersions = file.gameVersions;
        rec.folders = folders;
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;

        state_.upsert(rec);
        state_.save();

        emit updateApplied(rec);
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        fail(QString::fromStdString(e.what()));
    }
}

void WamWorker::setFlavor(qint64 modId, qint64 flavorTypeId) {
    auto existing = state_.find(modId);
    if (!existing) {
        emit errorOccurred("setFlavor", "Mod " + QString::number(modId) + " is not tracked.");
        return;
    }
    InstalledAddon rec = *existing;
    rec.flavorTypeId = flavorTypeId;
    rec.flavorName = client_ ? flavorNameFor(*client_, flavorTypeId) : std::string();
    state_.upsert(rec);
    state_.save();
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

void WamWorker::scan() {
    if (!config_.wow_path.has_value()) {
        emit errorOccurred("scan", "No WoW installation path configured.");
        emit scanFinished({});
        return;
    }
    try {
        const auto entries = Reconciler::scan(config_.addonsDir(), state_);
        const auto groups = Reconciler::groupByModId(entries);

        // Names and icons for .toc-tagged mods, batched. A nicety only: with
        // no key or no network the rows just fall back to "Mod <id>".
        std::map<int64_t, CurseForgeMod> known;
        if (client_) {
            std::vector<int64_t> ids;
            for (const auto& g : groups) if (g.first != 0) ids.push_back(g.first);
            try {
                for (size_t i = 0; i < ids.size(); i += 50) {
                    std::vector<int64_t> chunk(ids.begin() + i, ids.begin() + std::min(ids.size(), i + 50));
                    for (auto& m : client_->getMods(chunk)) known[m.id] = m;
                }
            } catch (const std::exception&) {}
        }

        auto describe = [](const ScanEntry& e) {
            QString line = QString::fromStdString(e.folder);
            if (e.toc.title) {
                const QString t = cleanTocText(QString::fromStdString(*e.toc.title));
                if (!t.isEmpty() && t != line) line += " (" + t + ")";
            }
            if (e.toc.version) line += " v" + QString::fromStdString(*e.toc.version);
            return line;
        };

        QList<ScanGroup> out;
        for (const auto& g : groups) {
            if (g.first == 0) {
                // Untagged: one row per folder, each needs its own mod id.
                for (const auto& e : g.second) {
                    ScanGroup sg;
                    sg.folders << QString::fromStdString(e.folder);
                    sg.details << describe(e);
                    QString title = e.toc.title ? cleanTocText(QString::fromStdString(*e.toc.title)) : QString();
                    sg.name = title.isEmpty() ? sg.folders.first() : title;
                    out.push_back(sg);
                }
                continue;
            }
            ScanGroup sg;
            sg.modId = g.first;
            for (const auto& e : g.second) {
                sg.folders << QString::fromStdString(e.folder);
                sg.details << describe(e);
            }
            auto it = known.find(g.first);
            if (it != known.end()) {
                sg.name = QString::fromStdString(it->second.name);
                sg.iconUrl = QString::fromStdString(it->second.logoUrl);
            } else {
                sg.name = QString("Mod %1").arg(g.first);
            }
            out.push_back(sg);
        }
        emit scanFinished(out);
    } catch (const std::exception& e) {
        emit errorOccurred("scan", QString::fromStdString(e.what()));
        emit scanFinished({});
    }
}

void WamWorker::adopt(qint64 modId, const QStringList& folders, bool includeSiblings, bool rescan) {
    if (!config_.wow_path.has_value()) {
        emit errorOccurred("adopt", "No WoW installation path configured.");
        return;
    }
    try {
        const auto dir = config_.addonsDir();
        std::vector<std::string> list;
        for (const auto& f : folders) list.push_back(f.toStdString());

        std::string name = "mod " + std::to_string(modId);
        std::string icon;
        if (client_) {
            try {
                auto mod = client_->getMod(modId);
                name = mod.name;
                icon = mod.logoUrl;
            } catch (const std::exception&) {} // adoption itself works offline

            if (includeSiblings) {
                try {
                    std::unordered_set<std::string> untracked;
                    for (const auto& e : Reconciler::scan(dir, state_)) untracked.insert(e.folder);
                    for (const auto& f : client_->getFiles(modId))
                        for (const auto& module : f.moduleNames)
                            if (untracked.count(module) &&
                                std::find(list.begin(), list.end(), module) == list.end())
                                list.push_back(module);
                } catch (const std::exception&) {} // best effort, same as the CLI
            }
        }

        auto rec = Reconciler::adopt(state_, dir, modId, name, icon, list);
        state_.save();
        emit adopted(modId, QString::fromStdString(rec.displayName), static_cast<int>(list.size()));
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        emit errorOccurred("adopt", QString::fromStdString(e.what()));
    }
    if (rescan) scan();
}

void WamWorker::backfillIcons() {
    if (!client_) return;
    std::vector<int64_t> ids;
    for (const auto& a : state_.all())
        if (a.modId != 0 && a.iconUrl.empty()) ids.push_back(a.modId);
    if (ids.empty()) return;

    bool changed = false;
    try {
        for (size_t i = 0; i < ids.size(); i += 50) {
            std::vector<int64_t> chunk(ids.begin() + i, ids.begin() + std::min(ids.size(), i + 50));
            for (const auto& mod : client_->getMods(chunk)) {
                if (mod.logoUrl.empty()) continue;
                auto existing = state_.find(mod.id);
                if (!existing) continue;
                InstalledAddon rec = *existing;
                rec.iconUrl = mod.logoUrl;
                state_.upsert(rec);
                changed = true;
            }
        }
    } catch (const std::exception&) {}
    if (changed) {
        state_.save();
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    }
}

void WamWorker::refreshAddonList() {
    state_ = StateStore::load();
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

void WamWorker::removeAddon(qint64 modId) {
    auto found = state_.find(modId);
    if (!found.has_value()) {
        emit errorOccurred("removeAddon", "No tracked addon with mod id " + QString::number(modId));
        return;
    }
    if (!config_.wow_path.has_value()) {
        emit errorOccurred("removeAddon", "No WoW installation path configured.");
        return;
    }
    try {
        AddonInstaller::removeFolders(config_.addonsDir(), found->folders);
        state_.remove(modId);
        state_.save();
        emit addonRemoved(modId);
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        emit errorOccurred("removeAddon", QString::fromStdString(e.what()));
    }
}

void WamWorker::untrackAddon(qint64 modId) {
    if (!state_.find(modId).has_value()) {
        emit errorOccurred("untrackAddon", "No tracked addon with mod id " + QString::number(modId));
        return;
    }
    state_.remove(modId);
    state_.save();
    emit addonUntracked(modId);
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

} // namespace wam::gui
