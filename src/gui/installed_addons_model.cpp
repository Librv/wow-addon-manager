#include "gui/installed_addons_model.hpp"
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
        case FlavorNameRole:       return QString::fromStdString(a.flavorName);
        case FlavorTypeIdRole:     return static_cast<qint64>(a.flavorTypeId);
        case IconUrlRole:          return QString::fromStdString(a.iconUrl);
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
    };
}

void InstalledAddonsModel::setAddons(const QList<wam::InstalledAddon>& addons) {
    beginResetModel();
    addons_ = addons;
    endResetModel();
}

} // namespace wam::gui
