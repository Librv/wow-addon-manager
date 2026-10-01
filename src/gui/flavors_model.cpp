#include "gui/flavors_model.hpp"

namespace wam::gui {

int FlavorsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : entries_.size();
}

QVariant FlavorsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size()) return {};
    const auto& e = entries_[index.row()];
    switch (role) {
        case SlugRole:     return e.slug;
        case FlavorIdRole: return e.id;
        case NameRole:     return e.name;
        case ApiNameRole:  return e.apiName;
        case EditedRole:   return !e.apiName.isEmpty() && e.name != e.apiName;
        default: return {};
    }
}

QHash<int, QByteArray> FlavorsModel::roleNames() const {
    return {{SlugRole, "slug"}, {FlavorIdRole, "flavorId"}, {NameRole, "name"},
            {ApiNameRole, "apiName"}, {EditedRole, "edited"}};
}

void FlavorsModel::setEntries(const QList<FlavorInfo>& entries) {
    bool sameRows = entries.size() == entries_.size();
    for (int i = 0; sameRows && i < entries.size(); ++i)
        sameRows = entries[i].slug == entries_[i].slug;

    if (sameRows) {
        entries_ = entries;
        if (!entries_.isEmpty()) emit dataChanged(index(0), index(entries_.size() - 1));
        return;
    }
    const int before = entries_.size();
    beginResetModel();
    entries_ = entries;
    endResetModel();
    if (before != entries_.size()) emit countChanged();
}

} // namespace wam::gui
