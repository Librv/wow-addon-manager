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

// "842", "1.5K", "45K", "1.2M": a download count in a few characters. Empty for 0 (unknown).
inline QString formatCount(qint64 n) {
    if (n <= 0) return {};
    static const char* suffix[] = {"", "K", "M", "B"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 999.5 && u < 3) { v /= 1000.0; ++u; }
    if (u == 0) return QString::number(n);
    QString s = QString::number(v, 'f', v < 9.95 ? 1 : 0);
    if (s.endsWith(QLatin1String(".0"))) s.chop(2);
    return s + QLatin1String(suffix[u]);
}

// "Sep 29, 2026, 21:14" in the local time zone.
inline QString formatIsoDateTimeLocal(const std::string& iso) {
    const QDateTime dt = parseIso(iso);
    return dt.isValid() ? QLocale::c().toString(dt.toLocalTime(), "MMM d, yyyy, HH:mm") : QString();
}

} // namespace wam::gui
