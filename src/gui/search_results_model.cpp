#include "gui/search_results_model.hpp"

namespace wam::gui {

int SearchResultsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : results_.size();
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= results_.size()) return {};
    const auto& m = results_[index.row()];
    switch (role) {
        case ModIdRole:      return static_cast<qint64>(m.id);
        case NameRole:       return QString::fromStdString(m.name);
        case SlugRole:       return QString::fromStdString(m.slug);
        case SummaryRole:    return QString::fromStdString(m.summary);
        case WebsiteUrlRole: return QString::fromStdString(m.websiteUrl);
        case LogoUrlRole:    return QString::fromStdString(m.logoUrl);
        default: return {};
    }
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const {
    return {
        {ModIdRole, "modId"}, {NameRole, "name"}, {SlugRole, "slug"},
        {SummaryRole, "summary"}, {WebsiteUrlRole, "websiteUrl"}, {LogoUrlRole, "logoUrl"},
    };
}

void SearchResultsModel::setResults(const QList<wam::CurseForgeMod>& results) {
    beginResetModel();
    results_ = results;
    endResetModel();
}

QList<qint64> SearchResultsModel::flavorIdsFor(qint64 modId) const {
    for (const auto& m : results_) {
        if (m.id != modId) continue;
        QList<qint64> out;
        for (auto id : m.gameVersionTypeIds) out.push_back(static_cast<qint64>(id));
        return out;
    }
    return {};
}

} // namespace wam::gui
