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
        FlavorNameRole, FlavorTypeIdRole, IconUrlRole,
        // Display texts and state for the expandable row
        DescriptionRole, VersionTextRole, ChannelTextRole, ReleasedTextRole, InstalledTextRole,
        SourceTextRole, LinkedRole, ModSlugRole, FileIdRole, ChangelogStateRole, ChangelogTextRole,
        ZipNameTextRole, AuthorRole
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

    // The changelog of an installed file, shown in the row's details. State is
    // "none" (not asked for), "loading", "ready" or "failed"; text is the
    // changelog, or the error when it failed. Kept by file id across reloads.
    void setChangelog(qint64 fileId, const QString& state, const QString& text);
    QString changelogState(qint64 fileId) const;
    qint64 fileIdFor(qint64 modId) const; // 0 if unknown or not linked
private:
    QString flavorNameFor(const wam::InstalledAddon& a) const;
    QString describe(const wam::InstalledAddon& a) const;

    struct Changelog { QString state, text; };
    QHash<qint64, Changelog> changelogs_;

    QList<wam::InstalledAddon> addons_;
    QHash<qint64, QString> flavorNames_;
};

} // namespace wam::gui
