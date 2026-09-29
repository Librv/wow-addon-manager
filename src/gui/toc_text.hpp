#pragma once
#include <QRegularExpression>
#include <QString>

namespace wam::gui {

// Addon .toc titles are often decorated with WoW's in-game markup, which is
// noise anywhere else: |cAARRGGBB ... |r colours and |T...|t inline textures.
// Strips both and trims the result.
inline QString cleanTocText(QString s) {
    static const QRegularExpression texture(R"(\|T[^|]*\|t)");
    static const QRegularExpression colorStart(R"(\|c[0-9a-fA-F]{8})");
    static const QRegularExpression colorReset(R"(\|r)");
    s.remove(texture);
    s.remove(colorStart);
    s.remove(colorReset);
    return s.trimmed();
}

} // namespace wam::gui
