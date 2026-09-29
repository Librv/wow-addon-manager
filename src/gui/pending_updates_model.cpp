#include "gui/pending_updates_model.hpp"

namespace wam::gui {

namespace {
QString statusName(PendingUpdate::Status s) {
    switch (s) {
        case PendingUpdate::Status::Pending:  return "pending";
        case PendingUpdate::Status::Applying: return "applying";
        case PendingUpdate::Status::Failed:   return "failed";
    }
    return "pending";
}
} // namespace

int PendingUpdatesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : items_.size();
}

QVariant PendingUpdatesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= items_.size()) return {};
    const auto& u = items_[index.row()];
    switch (role) {
        case ModIdRole:        return u.modId;
        case ModNameRole:      return u.modName;
        case ModSlugRole:      return u.modSlug;
        case FlavorNameRole:   return u.flavorName;
        case CurrentFileRole:  return u.currentFileName;
        case LatestFileRole:   return u.latestFileName;
        case LatestFileIdRole: return u.latestFileId;
        case BlockedRole:      return u.blocked;
        case StatusRole:       return statusName(u.status);
        case ErrorRole:        return u.error;
        case IconUrlRole:      return u.iconUrl;
        default: return {};
    }
}

QHash<int, QByteArray> PendingUpdatesModel::roleNames() const {
    return {
        {ModIdRole, "modId"}, {ModNameRole, "modName"}, {ModSlugRole, "modSlug"},
        {FlavorNameRole, "flavorName"}, {CurrentFileRole, "currentFile"},
        {LatestFileRole, "latestFile"}, {LatestFileIdRole, "latestFileId"},
        {BlockedRole, "blocked"}, {StatusRole, "status"}, {ErrorRole, "error"}, {IconUrlRole, "iconUrl"},
    };
}

int PendingUpdatesModel::applicableCount() const {
    int n = 0;
    for (const auto& u : items_)
        if (!u.blocked && u.status == PendingUpdate::Status::Pending) ++n;
    return n;
}

QVariantMap PendingUpdatesModel::head() const {
    // Always the same keys, even with nothing queued: QML bindings such as
    // `text: head.latestFile` evaluate while the view is hidden, and an
    // undefined value cannot be assigned to a string property.
    const PendingUpdate u = items_.isEmpty() ? PendingUpdate{} : items_.first();
    return {
        {"modId", u.modId}, {"modName", u.modName}, {"modSlug", u.modSlug},
        {"flavorName", u.flavorName}, {"currentFile", u.currentFileName},
        {"latestFile", u.latestFileName}, {"latestFileId", u.latestFileId},
        {"blocked", u.blocked}, {"status", statusName(u.status)}, {"error", u.error},
        {"iconUrl", u.iconUrl},
    };
}

int PendingUpdatesModel::indexOf(qint64 modId) const {
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].modId == modId) return i;
    return -1;
}

const PendingUpdate* PendingUpdatesModel::find(qint64 modId) const {
    int i = indexOf(modId);
    return i < 0 ? nullptr : &items_[i];
}

void PendingUpdatesModel::add(const PendingUpdate& u) {
    int i = indexOf(u.modId);
    if (i >= 0) {
        items_[i] = u;
        emit dataChanged(index(i), index(i));
    } else {
        beginInsertRows({}, items_.size(), items_.size());
        items_.push_back(u);
        endInsertRows();
    }
    emit changed();
}

void PendingUpdatesModel::remove(qint64 modId) {
    int i = indexOf(modId);
    if (i < 0) return;
    beginRemoveRows({}, i, i);
    items_.removeAt(i);
    endRemoveRows();
    emit changed();
}

void PendingUpdatesModel::clear() {
    beginResetModel();
    items_.clear();
    endResetModel();
    emit changed();
}

void PendingUpdatesModel::setStatus(qint64 modId, PendingUpdate::Status s, const QString& error) {
    int i = indexOf(modId);
    if (i < 0) return;
    items_[i].status = s;
    items_[i].error = error;
    emit dataChanged(index(i), index(i));
    emit changed();
}

} // namespace wam::gui
