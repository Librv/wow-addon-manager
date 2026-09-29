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

private:
    QList<wam::InstalledAddon> addons_;
};

} // namespace wam::gui
