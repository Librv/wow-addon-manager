#include "gui/installed_addons_model.hpp"
#include "gui/format.hpp"
#include <QStringList>

namespace wam::gui {

namespace {
QString channelName(ReleaseChannel c) {
    switch (c) {
        case ReleaseChannel::Release: return "release";
        case ReleaseChannel::Beta:    return "beta";
        case ReleaseChannel::Alpha:   return "alpha";
    }
    return "unknown";
}

QString capitalized(QString s) {
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

// What the file is called: CurseForge's name for it (e.g. "v9.3.2"), else its zip name.
QString versionOf(const wam::InstalledAddon& a) {
    const auto& name = !a.fileDisplayName.empty() ? a.fileDisplayName : a.fileName;
    return name.empty() ? QStringLiteral("Unknown") : QString::fromStdString(name);
}

// The zip's file name, shown after the version. Empty when it adds nothing:
// no zip name, or the version text already is the zip name.
QString zipNameOf(const wam::InstalledAddon& a) {
    if (a.fileId == 0 || a.fileName.empty() || a.fileDisplayName.empty() || a.fileDisplayName == a.fileName)
        return {};
    return QString::fromStdString(a.fileName);
}

QString sourceOf(const wam::InstalledAddon& a) {
    if (a.fileId == 0) return QStringLiteral("Adopted from an existing folder");
    if (a.adopted) return QStringLiteral("Adopted, linked to CurseForge");
    return a.manuallyProvided ? QStringLiteral("Manual file") : QStringLiteral("CurseForge download");
}
} // namespace

int InstalledAddonsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : addons_.size();
}

QVariant InstalledAddonsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= addons_.size()) return {};
    const auto& a = addons_[index.row()];
    switch (role) {
        case ModIdRole:            return static_cast<qint64>(a.modId);
        case DisplayNameRole:      return QString::fromStdString(a.displayName);
        case FileNameRole:         return QString::fromStdString(a.fileName);
        case ChannelRole:          return channelName(a.channel);
        case InstalledAtRole:      return QString::fromStdString(a.installedAt);
        case ManuallyProvidedRole: return a.manuallyProvided;
        case FlavorNameRole:       return flavorNameFor(a);
        case FlavorTypeIdRole:     return static_cast<qint64>(a.flavorTypeId);
        case IconUrlRole:          return QString::fromStdString(a.iconUrl);
        case AuthorRole:           return QString::fromStdString(a.author);
        case DescriptionRole:      return describe(a);
        case VersionTextRole:      return a.fileId == 0 ? QStringLiteral("Unknown until its first update") : versionOf(a);
        case ChannelTextRole:      return capitalized(channelName(a.channel));
        case ReleasedTextRole:     return formatIsoDate(a.fileDate);
        case InstalledTextRole:    return formatIsoDateTimeLocal(a.installedAt);
        case SourceTextRole:       return sourceOf(a);
        case ZipNameTextRole:      return zipNameOf(a);
        case ShortVersionTextRole: return a.fileId == 0 ? QString() : versionOf(a);
        case LinkedRole:           return a.fileId != 0;
        case ModSlugRole:          return QString::fromStdString(a.modSlug);
        case FileIdRole:           return static_cast<qint64>(a.fileId);
        case ChangelogStateRole:   return changelogs_.value(static_cast<qint64>(a.fileId)).state.isEmpty()
                                        ? QStringLiteral("none") : changelogs_.value(static_cast<qint64>(a.fileId)).state;
        case ChangelogTextRole:    return changelogs_.value(static_cast<qint64>(a.fileId)).text;
        case FoldersRole: {
            QStringList folders;
            for (const auto& f : a.folders) folders << QString::fromStdString(f);
            return folders;
        }
        default: return {};
    }
}

QHash<int, QByteArray> InstalledAddonsModel::roleNames() const {
    return {
        {ModIdRole, "modId"}, {DisplayNameRole, "displayName"}, {FileNameRole, "fileName"},
        {ChannelRole, "channel"}, {InstalledAtRole, "installedAt"},
        {ManuallyProvidedRole, "manuallyProvided"}, {FoldersRole, "folders"},
        {FlavorNameRole, "flavorName"}, {FlavorTypeIdRole, "flavorTypeId"}, {IconUrlRole, "iconUrl"},
        {DescriptionRole, "description"}, {VersionTextRole, "versionText"}, {ChannelTextRole, "channelText"},
        {ReleasedTextRole, "releasedText"}, {InstalledTextRole, "installedText"}, {SourceTextRole, "sourceText"},
        {LinkedRole, "linked"}, {ModSlugRole, "modSlug"}, {FileIdRole, "fileId"},
        {ChangelogStateRole, "changelogState"}, {ChangelogTextRole, "changelogText"},
        {ZipNameTextRole, "zipNameText"}, {AuthorRole, "author"},
        {ShortVersionTextRole, "shortVersionText"},
    };
}

QString InstalledAddonsModel::flavorNameFor(const wam::InstalledAddon& a) const {
    return flavorNames_.value(static_cast<qint64>(a.flavorTypeId), QString::fromStdString(a.flavorName));
}

QString InstalledAddonsModel::describe(const wam::InstalledAddon& a) const {
    const QString flavor = flavorNameFor(a);
    const QString channel = capitalized(channelName(a.channel));
    QStringList parts;
    if (a.fileId == 0) {
        parts << QStringLiteral("Adopted from your AddOns folder") << channel
              << (flavor.isEmpty() ? QStringLiteral("flavor not set") : flavor);
    } else {
        parts << versionOf(a) << channel;
        if (!flavor.isEmpty()) parts << flavor;
        const QString released = formatIsoDate(a.fileDate);
        if (!released.isEmpty()) parts << released;
    }
    return parts.join(QStringLiteral(" \u00b7 "));
}

void InstalledAddonsModel::setChangelog(qint64 fileId, const QString& state, const QString& text) {
    changelogs_.insert(fileId, {state, text});
    for (int i = 0; i < addons_.size(); ++i)
        if (static_cast<qint64>(addons_[i].fileId) == fileId)
            emit dataChanged(index(i), index(i), {ChangelogStateRole, ChangelogTextRole});
}

QString InstalledAddonsModel::changelogState(qint64 fileId) const {
    const QString s = changelogs_.value(fileId).state;
    return s.isEmpty() ? QStringLiteral("none") : s;
}

qint64 InstalledAddonsModel::fileIdFor(qint64 modId) const {
    for (const auto& a : addons_) if (static_cast<qint64>(a.modId) == modId) return static_cast<qint64>(a.fileId);
    return 0;
}

void InstalledAddonsModel::setFlavorNames(const QHash<qint64, QString>& names) {
    flavorNames_ = names;
    if (!addons_.isEmpty()) emit dataChanged(index(0), index(addons_.size() - 1), {FlavorNameRole});
}

void InstalledAddonsModel::setAddons(const QList<wam::InstalledAddon>& addons) {
    beginResetModel();
    addons_ = addons;
    endResetModel();
}

} // namespace wam::gui
