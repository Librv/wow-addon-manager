#include "gui/wam_worker.hpp"
#include "core/addon_installer.hpp"
#include "core/http_client.hpp"
#include "core/scoped_temp_file.hpp"
#include "core/reconciler.hpp"
#include "core/html_text.hpp"
#include "gui/toc_text.hpp"
#include <QList>
#include <QVariantMap>
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

const char* kInactiveNote = "This profile is not active, so the WoW folder holds another profile's addons. "
                            "Set it as the active profile first.";

} // namespace

WamWorker::WamWorker(QObject* parent) : QObject(parent) {}

CurseForgeClient* WamWorker::requireClient(const char* context) {
    if (!client_) {
        emit errorOccurred(context, "No CurseForge API key configured.");
        return nullptr;
    }
    return &*client_;
}

const Profile* WamWorker::selectedProfile() const {
    return profiles_.find(profiles_.selectedId());
}

std::optional<Profile> WamWorker::requireProfile(const char* context) {
    if (const auto* p = selectedProfile()) return *p;
    emit errorOccurred(context, "No profile is loaded.");
    return std::nullopt;
}

void WamWorker::emitConfig() {
    const Profile* p = selectedProfile();
    const bool hasPath = p && p->wowPath.has_value();
    emit configChanged(config_.curseforge_api_key.has_value(), hasPath,
                       hasPath ? QString::fromStdString(*p->wowPath) : QString(),
                       p ? static_cast<qint64>(p->flavorId.value_or(0)) : 0,
                       p ? QString::fromStdString(p->flavorName.value_or("")) : QString());
}

void WamWorker::emitProfiles() {
    QVariantList out;
    for (const auto& p : profiles_.all()) {
        // The selected profile's list is in memory; the others are read
        // from disk (small files, and only on profile changes).
        const int count = p.id == profiles_.selectedId()
            ? static_cast<int>(state_.all().size())
            : static_cast<int>(StateStore::peek(profiles_.statePath(p.id)).value_or(std::vector<InstalledAddon>{}).size());
        out.push_back(QVariantMap{
            {"id", static_cast<qint64>(p.id)},
            {"name", QString::fromStdString(p.name)},
            {"active", p.active},
            {"wowPath", QString::fromStdString(p.wowPath.value_or(""))},
            {"flavorName", QString::fromStdString(p.flavorName.value_or(""))},
            {"addonCount", count},
        });
    }
    emit profilesChanged(out, static_cast<qint64>(profiles_.selectedId()));
}

void WamWorker::emitAddons() {
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

void WamWorker::showSelected() {
    loadState();
    emitConfig(); // before profilesChanged: QML reads wowPath when the profiles change
    emitProfiles();
    emitAddons();
}

void WamWorker::collectGarbage() {
    try {
        AddonLibrary::collectGarbage(profiles_);
    } catch (const std::exception&) {} // a zip left behind costs disk space, nothing else
}

void WamWorker::emitFlavors() {
    QList<FlavorInfo> out;
    for (const auto& e : flavors_.all())
        out.push_back({static_cast<qint64>(e.id), QString::fromStdString(e.slug),
                       QString::fromStdString(e.name), QString::fromStdString(e.apiName)});
    emit flavorsChanged(out);
}

void WamWorker::refreshFlavors() {
    if (!client_) return;
    try {
        flavors_.merge(client_->listGameVersionTypes());
        flavors_.save();
        emitFlavors();
    } catch (const std::exception&) {} // offline: whatever is on disk stays in use
}

void WamWorker::detectFlavor() {
    std::vector<Profile> todo;
    for (const auto& p : profiles_.all())
        if (p.wowPath.has_value() && !p.flavorId.has_value()) todo.push_back(p);
    if (todo.empty()) return;
    if (flavors_.all().empty()) refreshFlavors(); // first run, or the file was removed
    bool changed = false;
    for (const auto& p : todo) {
        // Match the names as shown first, then CurseForge's own, so renaming a
        // flavor in Settings does not stop its folder from being recognised.
        const std::string folder = p.wowFolderName();
        auto id = CurseForgeClient::matchFlavorForFolder(flavors_.asTypes(), folder);
        if (!id.has_value()) id = CurseForgeClient::matchFlavorForFolder(flavors_.asTypes(true), folder);
        if (!id.has_value()) continue; // unknown folder name: the user picks one in Settings
        try {
            profiles_.setFlavor(p.id, *id, flavors_.nameFor(*id));
            changed = true;
        } catch (const std::exception& e) {
            emit errorOccurred("setWowPath", QString::fromStdString(e.what()));
        }
    }
    if (changed) {
        emitConfig();
        emitProfiles();
    }
}

void WamWorker::initialize() {
    try {
        config_ = Config::load();
    } catch (const std::exception& e) {
        emit errorOccurred("initialize", QString::fromStdString(e.what()));
    }
    if (config_.curseforge_api_key.has_value())
        client_.emplace(*config_.curseforge_api_key);

    // First run: makes the first profile from config.json and installed.json.
    try {
        profiles_ = ProfileStore::load();
    } catch (const std::exception& e) {
        emit errorOccurred("initialize", QString::fromStdString(e.what()));
    }

    // A previous run killed mid-update leaves the old folders in .wam-backup,
    // and one killed mid-switch leaves profiles/switch.json. Neither stops
    // startup: the user still gets the app, with the error shown.
    for (const auto& p : profiles_.all()) {
        if (!p.active || !p.wowPath.has_value()) continue;
        try {
            auto dir = p.addonsDir();
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
    try {
        if (ProfileOps::recoverSwitch(profiles_))
            emit errorOccurred("recovery", "Finished a profile switch that was interrupted.");
    } catch (const std::exception& e) {
        emit errorOccurred("recovery", "Could not finish an interrupted profile switch: " +
                                           QString::fromStdString(e.what()));
    }

    flavors_ = FlavorCache::load();
    emitFlavors();  // the cached flavors are usable before (or without) the network
    showSelected();
    refreshFlavors();
    detectFlavor();
}

void WamWorker::setApiKey(const QString& key) {
    config_.curseforge_api_key = key.toStdString();
    config_.save();
    client_.emplace(*config_.curseforge_api_key);
    emitConfig();
    refreshFlavors();
    detectFlavor();
}

void WamWorker::setWowPath(const QString& path) {
    auto prof = requireProfile("setWowPath");
    if (!prof) return;
    try {
        profiles_.setWowPath(prof->id, path.toStdString()); // clears the flavor of the old folder
    } catch (const std::exception& e) {
        emit errorOccurred("setWowPath", QString::fromStdString(e.what()));
        return;
    }
    emitConfig();
    emitProfiles();
    detectFlavor();
}

void WamWorker::setWowFlavor(qint64 flavorTypeId) {
    auto prof = requireProfile("setWowFlavor");
    if (!prof) return;
    try {
        if (flavorTypeId == 0) profiles_.setFlavor(prof->id, std::nullopt, std::nullopt);
        else profiles_.setFlavor(prof->id, flavorTypeId, flavors_.nameFor(flavorTypeId));
    } catch (const std::exception& e) {
        emit errorOccurred("setWowFlavor", QString::fromStdString(e.what()));
        return;
    }
    emitConfig();
    emitProfiles();
}

void WamWorker::selectProfile(qint64 id) {
    try {
        profiles_.select(id);
    } catch (const std::exception& e) {
        emit errorOccurred("selectProfile", QString::fromStdString(e.what()));
        return;
    }
    showSelected();
}

void WamWorker::createProfile(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred("createProfile", "A profile needs a name.");
        return;
    }
    try {
        profiles_.select(profiles_.create(trimmed.toStdString()));
    } catch (const std::exception& e) {
        emit errorOccurred("createProfile", QString::fromStdString(e.what()));
        return;
    }
    showSelected();
}

void WamWorker::renameProfile(qint64 id, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit errorOccurred("renameProfile", "A profile needs a name.");
        return;
    }
    try {
        profiles_.rename(id, trimmed.toStdString());
    } catch (const std::exception& e) {
        emit errorOccurred("renameProfile", QString::fromStdString(e.what()));
        return;
    }
    emitProfiles();
}

void WamWorker::deleteProfile(qint64 id) {
    const int64_t selectedBefore = profiles_.selectedId();
    try {
        profiles_.remove(id); // refuses the active and the last profile
    } catch (const std::exception& e) {
        emit errorOccurred("deleteProfile", QString::fromStdString(e.what()));
        return;
    }
    if (profiles_.selectedId() != selectedBefore) showSelected();
    else emitProfiles();
}

void WamWorker::activateProfile(qint64 id) {
    // Re-downloads a zip the library lacks, by file id, the same way an
    // install does. Through a temp file, so a broken download never sits in
    // the library looking like a usable zip.
    auto fetch = [this](const InstalledAddon& a, const std::filesystem::path& dest) {
        if (!client_ || a.fileId == 0) return false;
        auto file = client_->getFile(a.modId, a.fileId);
        if (file.isBlocked()) return false;
        ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(file.id) + "_" + file.fileName));
        if (!HttpClient::downloadToFile(*file.downloadUrl, tmpZip.path()).ok()) return false;
        AddonLibrary::store(tmpZip.path(), dest);
        return true;
    };
    try {
        ProfileOps::activate(profiles_, id, fetch);
        showSelected(); // the target's installed.json now has the folder sets the switch installed
        const auto* p = profiles_.find(id);
        emit profileActivated(id, p ? QString::fromStdString(p->name) : QString());
    } catch (const std::exception& e) {
        emit errorOccurred("activateProfile", QString::fromStdString(e.what()));
        showSelected(); // a switch that failed half way may still have moved the active flags
    }
    emit activateProfileFinished();
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

void WamWorker::renameFlavor(const QString& slug, const QString& name) {
    if (!flavors_.rename(slug.toStdString(), name.trimmed().toStdString())) {
        emit errorOccurred("renameFlavor", "Unknown flavor '" + slug + "'.");
        return;
    }
    flavors_.save();
    // Each profile stores its folder's flavor with its display name.
    bool changed = false;
    for (const auto& p : std::vector<Profile>(profiles_.all())) {
        if (!p.flavorId.has_value()) continue;
        const std::string newName = flavors_.nameFor(*p.flavorId, p.flavorName.value_or(""));
        if (newName == p.flavorName.value_or("")) continue;
        try {
            profiles_.setFlavor(p.id, p.flavorId, newName);
            changed = true;
        } catch (const std::exception&) {}
    }
    if (changed) {
        emitConfig();
        emitProfiles();
    }
    emitFlavors();
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
    auto prof = requireProfile("install");
    if (!prof) return;
    if (!prof->wowPath.has_value()) {
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

        ScopedTempFile tmpZip(Config::dataDir() / "tmp" /
                              (std::to_string(file.id) + "_" + file.fileName));
        auto dl = HttpClient::downloadToFile(*file.downloadUrl, tmpZip.path());
        if (!dl.ok()) {
            emit errorOccurred("install", "Download failed (HTTP " + QString::number(dl.status) + ").");
            return;
        }

        auto existing = state_.find(modId);
        InstalledAddon rec;
        rec.modId = modId;
        rec.displayName = mod.name;
        recordFile(rec, file);
        rec.modSlug = mod.slug;
        rec.author = mod.author;
        // Into the library, and on an active profile into AddOns/ too, where
        // reinstalling a tracked mod replaces its old folders atomically.
        rec.folders = ProfileOps::placeZip(*prof, rec, tmpZip.path(),
                                           existing ? existing->folders : std::vector<std::string>{});
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        rec.iconUrl = !mod.logoUrl.empty() ? mod.logoUrl : (existing ? existing->iconUrl : std::string());
        if (flavorTypeId != 0) {
            rec.flavorTypeId = flavorTypeId;
            rec.flavorName = flavors_.nameFor(flavorTypeId);
        } else if (existing.has_value()) {
            rec.flavorTypeId = existing->flavorTypeId;
            rec.flavorName = existing->flavorName;
        }

        state_.upsert(rec);
        state_.save();
        collectGarbage(); // the previous version's zip, unless another profile uses it

        emit installFinished(rec);
        emitAddons();
        emitProfiles(); // addon counts
    } catch (const std::exception& e) {
        emit errorOccurred("install", QString::fromStdString(e.what()));
    }
}

void WamWorker::installManual(qint64 modId, qint64 fileId, const QString& zipPath, qint64 flavorTypeId) {
    auto prof = requireProfile("installManual");
    if (!prof) return;
    if (!prof->wowPath.has_value()) {
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
        rec.fileDisplayName.clear(); // the previous version's details no longer apply
        rec.fileDate.clear();
        rec.channel = existing ? existing->channel : ReleaseChannel::Release;
        rec.gameVersions.clear();

        if (client_) { // best-effort metadata, same as the CLI; works offline without it
            try {
                auto mod = client_->getMod(modId);
                auto file = client_->getFile(modId, fileId);
                rec.displayName = mod.name;
                recordFile(rec, file);
                rec.modSlug = mod.slug;
                if (!mod.author.empty()) rec.author = mod.author;
                if (!mod.logoUrl.empty()) rec.iconUrl = mod.logoUrl;
            } catch (const std::exception&) {}
        }
        if (flavorTypeId != 0) {
            rec.flavorTypeId = flavorTypeId;
            rec.flavorName = flavors_.nameFor(flavorTypeId);
        }

        rec.adopted = false; // wam installed these files now
        rec.folders = ProfileOps::placeZip(*prof, rec, zip,
                                           existing ? existing->folders : std::vector<std::string>{});
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = true;

        state_.upsert(rec);
        state_.save();
        collectGarbage();
        emit installFinished(rec);
        emitAddons();
        emitProfiles();
    } catch (const std::exception& e) {
        emit errorOccurred("installManual", QString::fromStdString(e.what()));
    }
}

// The flavor to look files up in for an addon: its own, or for an adopted
// addon that has none yet, its profile's folder's (that is where it lives).
qint64 WamWorker::effectiveFlavor(const InstalledAddon& a) const {
    if (a.flavorTypeId != 0) return static_cast<qint64>(a.flavorTypeId);
    const Profile* p = selectedProfile();
    return p ? static_cast<qint64>(p->flavorId.value_or(0)) : 0;
}

void WamWorker::checkOneUpdate(qint64 modId) {
    checkUpdate(modId, QString());
    emit singleCheckFinished(modId);
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
        const qint64 flavorId = effectiveFlavor(*existing);
        auto files = client->getFiles(modId, optId(flavorId));
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
                             QString::fromStdString(flavors_.nameFor(flavorId, existing->flavorName)), current, *chosen);
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

void WamWorker::applyUpdate(qint64 modId, qint64 fileId, qint64 profileId) {
    auto fail = [&](const QString& msg) {
        emit errorOccurred("applyUpdate", msg);
        emit updateFailed(modId, msg);
    };

    // The update was found for one profile; requests queue in order, so
    // another may have been selected before this one ran.
    if (profileId != profiles_.selectedId())
        return fail("Not applied: another profile was selected before the update ran.");
    const Profile* prof = selectedProfile();
    auto existing = state_.find(modId);
    if (!existing) return fail("Mod " + QString::number(modId) + " is not tracked.");
    if (!client_) return fail("No CurseForge API key configured.");
    if (!prof || !prof->wowPath) return fail("No WoW installation path configured.");

    try {
        auto file = client_->getFile(modId, fileId); // re-fetch: the files list can move between check and apply
        auto mod = client_->getMod(modId);            // for the page slug
        if (file.isBlocked()) {
            emit downloadBlocked(modId, QString::fromStdString(mod.name), QString::fromStdString(mod.slug),
                                 file.id, QString::fromStdString(file.fileName));
            return fail("Third-party downloads are blocked by the author.");
        }

        ScopedTempFile tmpZip(Config::dataDir() / "tmp" / (std::to_string(file.id) + "_" + file.fileName));
        auto dl = HttpClient::downloadToFile(*file.downloadUrl, tmpZip.path());
        if (!dl.ok()) return fail("Download failed (HTTP " + QString::number(dl.status) + ").");

        InstalledAddon rec = *existing; // keeps flavorTypeId/flavorName
        recordFile(rec, file);
        rec.modSlug = mod.slug;
        if (!mod.author.empty()) rec.author = mod.author;
        // Stage, then swap with rollback: a failure anywhere leaves the
        // installed version exactly as it was. Library only when inactive.
        rec.folders = ProfileOps::placeZip(*prof, rec, tmpZip.path(), existing->folders);
        rec.installedAt = nowIso8601();
        rec.manuallyProvided = false;
        rec.adopted = false; // wam downloaded and installed these files now
        if (rec.flavorTypeId == 0 && effectiveFlavor(rec) != 0) { // an adopted addon learns its flavor here
            rec.flavorTypeId = effectiveFlavor(rec);
            rec.flavorName = flavors_.nameFor(rec.flavorTypeId);
        }

        state_.upsert(rec);
        state_.save();
        collectGarbage();

        emit updateApplied(rec);
        emitAddons();
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
    rec.flavorName = flavors_.nameFor(flavorTypeId);
    state_.upsert(rec);
    state_.save();
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

void WamWorker::scan() {
    const Profile* prof = selectedProfile();
    if (!prof || !prof->wowPath.has_value()) {
        emit errorOccurred("scan", "No WoW installation path configured.");
        emit scanFinished({});
        return;
    }
    if (!prof->active) {
        emit errorOccurred("scan", kInactiveNote);
        emit scanFinished({});
        return;
    }
    try {
        const auto entries = Reconciler::scan(prof->addonsDir(), state_);
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
                sg.author = QString::fromStdString(it->second.author);
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

void WamWorker::adopt(qint64 modId, const QStringList& folders, bool includeSiblings, bool rescan,
                      qint64 profileId) {
    const Profile* prof = selectedProfile();
    if (profileId != profiles_.selectedId()) {
        // The scan these folders came from was for another profile.
        emit errorOccurred("adopt", "Not adopted: another profile was selected before it ran.");
        return;
    }
    if (!prof || !prof->wowPath.has_value()) {
        emit errorOccurred("adopt", "No WoW installation path configured.");
        return;
    }
    if (!prof->active) {
        emit errorOccurred("adopt", kInactiveNote);
        return;
    }
    try {
        const auto dir = prof->addonsDir();
        std::vector<std::string> list;
        for (const auto& f : folders) list.push_back(f.toStdString());

        std::string name = "mod " + std::to_string(modId);
        std::string icon, author;
        if (client_) {
            try {
                auto mod = client_->getMod(modId);
                name = mod.name;
                icon = mod.logoUrl;
                author = mod.author;
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
        if (rec.author.empty() && !author.empty()) {
            rec.author = author;
            state_.upsert(rec);
        }
        state_.save();
        emit adopted(modId, QString::fromStdString(rec.displayName), static_cast<int>(list.size()));
        emitAddons();
        emitProfiles();
    } catch (const std::exception& e) {
        emit errorOccurred("adopt", QString::fromStdString(e.what()));
    }
    if (rescan) scan();
}

void WamWorker::backfillDetails() {
    if (!client_) return;
    bool changed = false;

    // Icons, page slugs and authors, from the mods: one batched request per 50.
    std::vector<int64_t> modIds;
    for (const auto& a : state_.all())
        if (a.modId != 0 && (a.iconUrl.empty() || a.author.empty() || (a.fileId != 0 && a.modSlug.empty()))) modIds.push_back(a.modId);
    try {
        for (size_t i = 0; i < modIds.size(); i += 50) {
            std::vector<int64_t> chunk(modIds.begin() + i, modIds.begin() + std::min(modIds.size(), i + 50));
            for (const auto& mod : client_->getMods(chunk)) {
                auto existing = state_.find(mod.id);
                if (!existing) continue;
                InstalledAddon rec = *existing;
                if (rec.iconUrl.empty() && !mod.logoUrl.empty()) rec.iconUrl = mod.logoUrl;
                if (rec.modSlug.empty()) rec.modSlug = mod.slug;
                if (rec.author.empty()) rec.author = mod.author;
                if (rec.iconUrl != existing->iconUrl || rec.modSlug != existing->modSlug ||
                    rec.author != existing->author) {
                    state_.upsert(rec);
                    changed = true;
                }
            }
        }
    } catch (const std::exception&) {}

    // Version names and upload dates of installed files that predate them.
    std::vector<int64_t> fileIds;
    for (const auto& a : state_.all())
        if (a.fileId != 0 && (a.fileDate.empty() || a.fileDisplayName.empty())) fileIds.push_back(a.fileId);
    try {
        for (size_t i = 0; i < fileIds.size(); i += 50) {
            std::vector<int64_t> chunk(fileIds.begin() + i, fileIds.begin() + std::min(fileIds.size(), i + 50));
            for (const auto& file : client_->getFilesByIds(chunk)) {
                auto existing = state_.find(file.modId);
                if (!existing || existing->fileId != file.id) continue;
                InstalledAddon rec = *existing;
                if (rec.fileDisplayName.empty()) rec.fileDisplayName = file.displayName;
                if (rec.fileDate.empty()) rec.fileDate = file.fileDate;
                if (rec.fileDisplayName != existing->fileDisplayName || rec.fileDate != existing->fileDate) {
                    state_.upsert(rec);
                    changed = true;
                }
            }
        }
    } catch (const std::exception&) {}

    if (changed) {
        state_.save();
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    }
}

void WamWorker::linkFile(qint64 modId, qint64 fileId, qint64 flavorTypeId) {
    auto existing = state_.find(modId);
    if (!existing) {
        emit errorOccurred("link", "Mod " + QString::number(modId) + " is not tracked.");
        return;
    }
    auto* client = requireClient("link");
    if (!client) return;
    try {
        auto mod = client->getMod(modId);
        auto file = client->getFile(modId, fileId);

        // Only bookkeeping: nothing is downloaded and nothing on disk changes.
        InstalledAddon rec = *existing;
        rec.displayName = mod.name;
        recordFile(rec, file);
        rec.modSlug = mod.slug;
        if (!mod.author.empty()) rec.author = mod.author;
        if (!mod.logoUrl.empty()) rec.iconUrl = mod.logoUrl;
        // The flavor comes from the flavor the file was picked under; CurseForge
        // only lists the file there if it is built for that flavor.
        if (flavorTypeId != 0) {
            rec.flavorTypeId = flavorTypeId;
            rec.flavorName = flavors_.nameFor(flavorTypeId);
        }

        state_.upsert(rec);
        state_.save();
        emit linked(modId, QString::fromStdString(rec.displayName));
        emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
    } catch (const std::exception& e) {
        emit errorOccurred("link", QString::fromStdString(e.what()));
    }
}

void WamWorker::loadChangelog(qint64 modId, qint64 fileId) {
    if (!client_) {
        emit changelogFailed(fileId, "No CurseForge API key configured.");
        return;
    }
    try {
        emit changelogLoaded(fileId, QString::fromStdString(htmlToPlainText(client_->getFileChangelog(modId, fileId))));
    } catch (const std::exception& e) {
        emit changelogFailed(fileId, QString::fromStdString(e.what()));
    }
}

void WamWorker::loadState() {
    const Profile* prof = selectedProfile();
    if (!prof) {
        state_ = StateStore{};
        return;
    }
    try {
        state_ = StateStore::load(profiles_.statePath(prof->id));
    } catch (const std::exception& e) {
        emit errorOccurred("initialize", QString::fromStdString(e.what()));
        return;
    }
    if (!state_.damagedFile().empty())
        emit errorOccurred("initialize", "The list of installed addons could not be read, so it starts empty. "
                                         "The old file was kept as " + QString::fromStdString(state_.damagedFile().string()) +
                                         ". Scan your AddOns folder to find your addons again.");
}

void WamWorker::refreshAddonList() {
    loadState();
    emit addonListLoaded(toQList<wam::InstalledAddon>(state_.all()));
}

void WamWorker::removeAddon(qint64 modId) {
    auto found = state_.find(modId);
    if (!found.has_value()) {
        emit errorOccurred("removeAddon", "No tracked addon with mod id " + QString::number(modId));
        return;
    }
    auto prof = requireProfile("removeAddon");
    if (!prof) return;
    try {
        ProfileOps::removeAddon(*prof, *found); // AddOns/ only when the profile is active
        state_.remove(modId);
        state_.save();
        collectGarbage();
        emit addonRemoved(modId);
        emitAddons();
        emitProfiles();
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
    collectGarbage();
    emit addonUntracked(modId);
    emitAddons();
    emitProfiles();
}

} // namespace wam::gui
