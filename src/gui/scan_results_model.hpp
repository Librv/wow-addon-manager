#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace wam::gui {

// One row of the "untracked addons" scan. A tagged group is every untracked
// folder whose .toc claims the same CurseForge mod (modId != 0). Untagged
// folders each get a row of their own (modId == 0), since each one needs its
// own mod id assigned by hand.
struct ScanGroup {
    qint64 modId = 0;
    QString name;        // CurseForge mod name if known, else the .toc title / folder name
    QString iconUrl;     // CurseForge logo thumbnail if known
    QString author;      // CurseForge author name(s) if known
    QStringList folders; // top-level AddOns folders in this row
    QStringList details; // one display line per folder, e.g. "DBM-Core (Deadly Boss Mods) v10.2.5"
};

} // namespace wam::gui

Q_DECLARE_METATYPE(wam::gui::ScanGroup)

namespace wam::gui {

class ScanResultsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY changed)   // untracked folders across all rows
    Q_PROPERTY(int matchedCount READ matchedCount NOTIFY changed) // rows a .toc tag already identified
public:
    enum Role { ModIdRole = Qt::UserRole + 1, NameRole, IconUrlRole, FoldersRole, DetailsRole, MatchedRole, AuthorRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setGroups(const QList<ScanGroup>& groups);
    QList<ScanGroup> groups() const { return groups_; }
    int folderCount() const;
    int matchedCount() const;

signals:
    void changed();

private:
    QList<ScanGroup> groups_;
};

} // namespace wam::gui
