#include "gui/scan_results_model.hpp"

namespace wam::gui {

int ScanResultsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : groups_.size();
}

QVariant ScanResultsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= groups_.size()) return {};
    const auto& g = groups_[index.row()];
    switch (role) {
        case ModIdRole:   return g.modId;
        case NameRole:    return g.name;
        case IconUrlRole: return g.iconUrl;
        case FoldersRole: return g.folders;
        case DetailsRole: return g.details;
        case MatchedRole: return g.modId != 0;
        case AuthorRole:  return g.author;
        default: return {};
    }
}

QHash<int, QByteArray> ScanResultsModel::roleNames() const {
    return {
        {ModIdRole, "modId"}, {NameRole, "name"}, {IconUrlRole, "iconUrl"},
        {FoldersRole, "folders"}, {DetailsRole, "details"}, {MatchedRole, "matched"}, {AuthorRole, "author"},
    };
}

void ScanResultsModel::setGroups(const QList<ScanGroup>& groups) {
    beginResetModel();
    groups_ = groups;
    endResetModel();
    emit changed();
}

int ScanResultsModel::folderCount() const {
    int n = 0;
    for (const auto& g : groups_) n += g.folders.size();
    return n;
}

int ScanResultsModel::matchedCount() const {
    int n = 0;
    for (const auto& g : groups_) if (g.modId != 0) ++n;
    return n;
}

} // namespace wam::gui
