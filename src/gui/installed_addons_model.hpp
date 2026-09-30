#pragma once
#include <QAbstractListModel>
#include <QList>
#include "core/state_store.hpp"

namespace wam::gui {

class InstalledAddonsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        ModIdRole = Qt::UserRole + 1, DisplayNameRole, FileNameRole,
        ChannelRole, InstalledAtRole, ManuallyProvidedRole, FoldersRole,
        FlavorNameRole, FlavorTypeIdRole, IconUrlRole
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAddons(const QList<wam::InstalledAddon>& addons);
    // Current display names by flavor id (from the flavor cache). A row shows
    // this in preference to the name stored when it was installed, so a rename
    // in Settings shows up everywhere.
    void setFlavorNames(const QHash<qint64, QString>& names);
private:
    QString flavorNameFor(const wam::InstalledAddon& a) const;

    QList<wam::InstalledAddon> addons_;
    QHash<qint64, QString> flavorNames_;
};

} // namespace wam::gui
