#pragma once
#include <QAbstractListModel>
#include <QList>
#include "core/curseforge_client.hpp"

namespace wam::gui {

class SearchResultsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { ModIdRole = Qt::UserRole + 1, NameRole, SlugRole, SummaryRole, WebsiteUrlRole, LogoUrlRole, AuthorRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setResults(const QList<wam::CurseForgeMod>& results);

    // gameVersionTypeIds (flavors) the given search result has files for;
    // empty if the mod isn't in the current results or reports none.
    QList<qint64> flavorIdsFor(qint64 modId) const;

private:
    QList<wam::CurseForgeMod> results_;
};

} // namespace wam::gui
