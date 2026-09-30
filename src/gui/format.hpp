#pragma once
#include <QDateTime>
#include <QLocale>
#include <QString>
#include <string>

namespace wam::gui {

inline QDateTime parseIso(const std::string& iso) {
    if (iso.empty()) return {};
    QDateTime dt = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    return dt;
}

// "Sep 29, 2026" (the date as it is in UTC, like CurseForge's site). Empty if
// the text is empty or not a date.
inline QString formatIsoDate(const std::string& iso) {
    const QDateTime dt = parseIso(iso);
    return dt.isValid() ? QLocale::c().toString(dt.toUTC().date(), "MMM d, yyyy") : QString();
}

// "Sep 29, 2026, 21:14" in the local time zone.
inline QString formatIsoDateTimeLocal(const std::string& iso) {
    const QDateTime dt = parseIso(iso);
    return dt.isValid() ? QLocale::c().toString(dt.toLocalTime(), "MMM d, yyyy, HH:mm") : QString();
}

} // namespace wam::gui
