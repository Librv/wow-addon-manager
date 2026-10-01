#include "gui/install_files_model.hpp"
#include "gui/format.hpp"
#include <algorithm>

namespace wam::gui {

InstallFile InstallFile::fromCurseForge(const wam::CurseForgeFile& f) {
    InstallFile out;
    out.fileId = f.id;
    out.displayName = QString::fromStdString(f.displayName.empty() ? f.fileName : f.displayName);
    out.fileName = QString::fromStdString(f.fileName);
    switch (f.releaseType) {
        case ReleaseChannel::Release: out.channel = "release"; break;
        case ReleaseChannel::Beta:    out.channel = "beta"; break;
        case ReleaseChannel::Alpha:   out.channel = "alpha"; break;
    }
    if (!f.gameVersions.empty()) {
        out.gameVersions = QString::fromStdString(f.gameVersions.front());
        if (f.gameVersions.size() > 1) out.gameVersions += QString(" +%1").arg(f.gameVersions.size() - 1);
    }
    out.date = formatIsoDate(f.fileDate);
    out.blocked = f.isBlocked();
    return out;
}

int InstallFilesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant InstallFilesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size()) return {};
    const auto& f = rows_[index.row()];
    switch (role) {
        case FileIdRole:       return f.fileId;
        case DisplayNameRole:  return f.displayName;
        case FileNameRole:     return f.fileName;
        case ChannelRole:      return f.channel;
        case GameVersionsRole: return f.gameVersions;
        case DateRole:         return f.date;
        case BlockedRole:      return f.blocked;
        default: return {};
    }
}

QHash<int, QByteArray> InstallFilesModel::roleNames() const {
    return {
        {FileIdRole, "fileId"}, {DisplayNameRole, "displayName"}, {FileNameRole, "fileName"},
        {ChannelRole, "channel"}, {GameVersionsRole, "gameVersions"}, {DateRole, "date"},
        {BlockedRole, "blocked"},
    };
}

qint64 InstallFilesModel::fileIdAt(int row) const {
    return row >= 0 && row < rows_.size() ? rows_[row].fileId : 0;
}

int InstallFilesModel::filteredCount() const {
    int n = 0;
    for (const auto& f : all_) if (channel_.isEmpty() || f.channel == channel_) ++n;
    return n;
}

bool InstallFilesModel::hasMore() const {
    return filteredCount() > limit_ || serverHasMore_;
}

void InstallFilesModel::rebuild() {
    beginResetModel();
    rows_.clear();
    for (const auto& f : all_) {
        if (rows_.size() >= limit_) break;
        if (channel_.isEmpty() || f.channel == channel_) rows_.push_back(f);
    }
    endResetModel();
    emit changed();
}

void InstallFilesModel::begin(qint64 modId, qint64 flavorId) {
    modId_ = modId;
    flavorId_ = flavorId;
    all_.clear();
    limit_ = kPageStep;
    nextIndex_ = 0;
    serverHasMore_ = false;
    loading_ = true;
    loaded_ = false;
    error_.clear();
    rebuild();
}

void InstallFilesModel::clear() {
    modId_ = 0;
    flavorId_ = 0;
    all_.clear();
    limit_ = kPageStep;
    nextIndex_ = 0;
    serverHasMore_ = false;
    loading_ = false;
    loaded_ = false;
    error_.clear();
    rebuild();
}

void InstallFilesModel::setLoading(bool loading) {
    if (loading_ == loading) return;
    loading_ = loading;
    emit changed();
}

void InstallFilesModel::append(const QList<InstallFile>& files, int index, int totalCount) {
    for (const auto& f : files) {
        const bool known = std::any_of(all_.begin(), all_.end(), [&](const InstallFile& e) { return e.fileId == f.fileId; });
        if (!known) all_.push_back(f);
    }
    // Newest first: a higher file id is a later upload.
    std::stable_sort(all_.begin(), all_.end(), [](const InstallFile& a, const InstallFile& b) { return a.fileId > b.fileId; });
    nextIndex_ = index + files.size();
    serverHasMore_ = !files.isEmpty() && nextIndex_ < totalCount;
    loading_ = false;
    loaded_ = true;
    error_.clear();
    rebuild();
}

void InstallFilesModel::fail(const QString& message) {
    loading_ = false;
    error_ = message;
    emit changed();
}

void InstallFilesModel::setChannel(const QString& channel) {
    if (channel_ == channel) return;
    channel_ = channel;
    limit_ = kPageStep;
    rebuild();
}

void InstallFilesModel::showMore() {
    limit_ += kPageStep;
    rebuild();
}

} // namespace wam::gui
