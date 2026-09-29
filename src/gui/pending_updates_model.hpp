#pragma once
#include <QAbstractListModel>
#include <QVariantMap>
#include <QList>

namespace wam::gui {

// One addon with an update waiting for review.
struct PendingUpdate {
    enum class Status { Pending, Applying, Failed };
    qint64 modId = 0;
    QString modName, modSlug, flavorName, currentFileName, latestFileName, iconUrl;
    qint64 latestFileId = 0;
    bool blocked = false; // author disabled third-party downloads: needs a manual zip
    Status status = Status::Pending;
    QString error;
};

// The review queue. Row 0 is the diff on screen ("head"); an applied or
// skipped row leaves the queue, so the next diff becomes the head.
class PendingUpdatesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int applicableCount READ applicableCount NOTIFY changed) // pending and not blocked
    Q_PROPERTY(QVariantMap head READ head NOTIFY changed)               // row 0; a full map of empty defaults when the queue is empty
public:
    enum Role { ModIdRole = Qt::UserRole + 1, ModNameRole, ModSlugRole, FlavorNameRole,
                CurrentFileRole, LatestFileRole, LatestFileIdRole, BlockedRole, StatusRole, ErrorRole, IconUrlRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return items_.size(); }
    int applicableCount() const;
    QVariantMap head() const;
    QList<PendingUpdate> items() const { return items_; }
    const PendingUpdate* find(qint64 modId) const;

    void add(const PendingUpdate& u); // replaces an existing entry for the same modId
    void remove(qint64 modId);
    void clear();
    void setStatus(qint64 modId, PendingUpdate::Status s, const QString& error = {});

signals:
    void changed();

private:
    int indexOf(qint64 modId) const;
    QList<PendingUpdate> items_;
};

} // namespace wam::gui
