#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>
#include "core/curseforge_client.hpp"

namespace wam::gui {

// One version of an addon offered in the install dialog.
struct InstallFile {
    qint64 fileId = 0;
    QString displayName;   // e.g. "v9.3.2"
    QString fileName;
    QString channel;       // "release" | "beta" | "alpha"
    QString gameVersions;  // short form, e.g. "12.1.0 +5"
    QString date;          // e.g. "Sep 29, 2026"; empty if the API gave none
    bool blocked = false;  // author disabled third-party downloads

    static InstallFile fromCurseForge(const wam::CurseForgeFile& f);
};

// The versions listed in the install dialog for one addon and flavor. It
// holds every file fetched so far (newest first, by file id) but exposes only
// those matching the chosen release channel, a few at a time. "Show more"
// widens that window, and asks for another page from the server only once
// everything already loaded is on screen.
class InstallFilesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)      // at least one page arrived for the current request
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString channel READ channel NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)    // more versions than shown, or more on the server
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(QVariantMap channelCounts READ channelCounts NOTIFY changed) // loaded versions per channel
    Q_PROPERTY(bool partial READ partial NOTIFY changed)    // the server has pages not loaded yet, so counts may grow
public:
    enum Role { FileIdRole = Qt::UserRole + 1, DisplayNameRole, FileNameRole, ChannelRole,
                GameVersionsRole, DateRole, BlockedRole };
    static constexpr int kPageStep = 5;

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loading() const { return loading_; }
    bool loaded() const { return loaded_; }
    QString error() const { return error_; }
    QString channel() const { return channel_; }
    bool hasMore() const;
    QVariantMap channelCounts() const;
    bool partial() const { return serverHasMore_; }
    Q_INVOKABLE qint64 fileIdAt(int row) const;

    // ---- driven by WamController
    void begin(qint64 modId, qint64 flavorId);              // new request: forget everything, now loading
    void clear();                                           // forget everything, not loading
    bool matches(qint64 modId, qint64 flavorId) const { return modId == modId_ && flavorId == flavorId_; }
    qint64 modIdValue() const { return modId_; }
    qint64 flavorIdValue() const { return flavorId_; }
    void setLoading(bool loading);
    void append(const QList<InstallFile>& files, int index, int totalCount);
    void fail(const QString& message);
    void setChannel(const QString& channel);
    void showMore();
    int nextIndex() const { return nextIndex_; }            // where the next server page starts
    int filteredCount() const;
    // True when the window is not full yet but the server has more to give.
    bool wantsFetch() const { return !loading_ && serverHasMore_ && filteredCount() < limit_; }

signals:
    void changed();

private:
    void rebuild();

    QList<InstallFile> all_;   // newest first
    QList<InstallFile> rows_;  // filtered by channel_, at most limit_
    qint64 modId_ = 0, flavorId_ = 0;
    QString channel_ = "release";
    QString error_;
    int limit_ = kPageStep;
    int nextIndex_ = 0;
    bool serverHasMore_ = false;
    bool loading_ = false;
    bool loaded_ = false;
};

} // namespace wam::gui
