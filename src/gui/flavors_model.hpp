#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QMetaType>
#include <QString>

namespace wam::gui {

// One entry of the flavor cache as the GUI sees it.
struct FlavorInfo {
    qint64 id = 0;
    QString slug;    // CurseForge's key, e.g. "wow-forever"
    QString name;    // display name (what every list and label shows)
    QString apiName; // CurseForge's own name
};

} // namespace wam::gui

Q_DECLARE_METATYPE(wam::gui::FlavorInfo)

namespace wam::gui {

// The flavor list the Settings page edits. Updates keep the rows (and the
// text field being typed in) alive when only names change.
class FlavorsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { SlugRole = Qt::UserRole + 1, FlavorIdRole, NameRole, ApiNameRole, EditedRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QList<FlavorInfo>& entries);
    QList<FlavorInfo> entries() const { return entries_; }

signals:
    void countChanged();

private:
    QList<FlavorInfo> entries_;
};

} // namespace wam::gui
