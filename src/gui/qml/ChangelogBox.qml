import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A changelog (markdown) on its own darker panel. When it is longer than
// `collapsedLines` it is cut at the last word that fits, followed by "…" and
// a link-coloured "Show more" on that same line. Expanded, "Show less" sits
// under the text. Clicking the text itself does not toggle anything; links in
// the changelog open in the browser.
//
// `loadState` is the model's changelogState: "ready" renders `text` as
// markdown, anything else shows a short plain message (loading, failed, ...).
Rectangle {
    id: root

    // ---- Tunables ---------------------------------------------------------
    property int collapsedLines: 6
    property real darkening: 1.12                       // Qt.darker factor for the panel
    property real padding: Kirigami.Units.largeSpacing
    property color linkColor: Kirigami.Theme.linkColor  // "Show more" / "Show less"
    property string moreText: qsTr("Show more")
    property string lessText: qsTr("Show less")
    // ------------------------------------------------------------------------

    property string loadState: "none"   // `changelogState` on the model ("state" is taken by Item)
    property string text
    property bool expanded: false

    readonly property bool isMarkdown: loadState === "ready" && text.length > 0
    readonly property string shownText: loadState === "ready"
        ? (text.length > 0 ? text : qsTr("No changelog was provided for this version."))
        : loadState === "failed" ? qsTr("Could not load the changelog: %1").arg(text)
        : qsTr("Loading…")

    // Changelogs are often one entry per line without blank lines between
    // them. Markdown would join those into one paragraph, so single newlines
    // become hard line breaks.
    readonly property string markdownText: shownText.replace(/([^\n\s])[ \t]*\n(?=[^\n])/g, "$1  \n")

    // Filled in by relayout(): is it longer than collapsedLines, and where does the "…" go.
    property bool collapsible: false
    property real shownHeight: 0
    property real cutX: 0
    property real cutY: 0
    property real cutHeight: 0

    color: Qt.darker(Kirigami.Theme.backgroundColor, darkening)
    radius: Kirigami.Units.smallSpacing
    implicitHeight: body.height + padding * 2

    FontMetrics { id: fm; font: edit.font }

    function isSpace(from) { return /\s/.test(edit.getText(from, from + 1)) }

    function relayout() {
        const full = edit.contentHeight
        const limit = collapsedLines * fm.lineSpacing
        collapsible = isMarkdown && edit.width > 0 && full > limit + 1
        if (!collapsible || expanded) {
            shownHeight = full
            return
        }

        // The last line that fits completely inside the limit.
        let y = limit - 1
        let r = edit.positionToRectangle(edit.positionAt(1, y))
        for (let i = 0; i < 64 && r.y + r.height > limit + 0.5 && r.y > 0; ++i)
            r = edit.positionToRectangle(edit.positionAt(1, Math.max(0, r.y - 1)))
        const mid = r.y + r.height / 2
        const lineStart = edit.positionAt(0, mid)
        const lineEnd = edit.positionAt(100000, mid)

        // Where to cut so "… Show more" fits on this line, on a word boundary.
        const maxX = edit.width - tail.implicitWidth
        let p = lineEnd
        if (edit.positionToRectangle(p).x > maxX) {
            p = edit.positionAt(maxX, mid)
            let q = p
            while (q > lineStart && !isSpace(q - 1)) --q   // back to the start of the word
            if (q > lineStart) p = q
        }
        while (p > lineStart && isSpace(p - 1)) --p        // no space before the "…"

        const c = edit.positionToRectangle(p)
        cutX = c.x
        cutY = r.y
        cutHeight = r.height
        shownHeight = r.y + r.height
    }
    function schedule() { Qt.callLater(relayout) }

    onExpandedChanged: schedule()
    onTextChanged: schedule()
    onLoadStateChanged: schedule()
    onWidthChanged: schedule()
    onCollapsedLinesChanged: schedule()
    Connections {
        target: edit
        function onContentHeightChanged() { root.schedule() }
        function onWidthChanged() { root.schedule() }
    }
    Connections {
        target: tail
        function onImplicitWidthChanged() { root.schedule() }
    }
    Component.onCompleted: schedule()

    Item {
        id: body
        x: root.padding
        y: root.padding
        width: root.width - root.padding * 2
        // Collapsed: only up to the cut line. Expanded: everything plus "Show less" when it can be collapsed again.
        height: root.expanded
                ? edit.contentHeight + (root.collapsible ? less.height + Kirigami.Units.smallSpacing : 0)
                : root.shownHeight
        clip: true

        TextEdit {
            id: edit
            width: body.width
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: root.isMarkdown ? TextEdit.MarkdownText : TextEdit.PlainText
            text: root.isMarkdown ? root.markdownText : root.shownText
            font: Kirigami.Theme.defaultFont
            color: Kirigami.Theme.textColor
            selectedTextColor: Kirigami.Theme.highlightedTextColor
            selectionColor: Kirigami.Theme.highlightColor
            opacity: root.isMarkdown ? 1 : 0.7
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }

        // Covers the end of the last visible line and puts "… Show more" there.
        Rectangle {
            visible: root.collapsible && !root.expanded
            x: root.cutX
            y: root.cutY
            width: body.width - root.cutX
            height: root.cutHeight
            color: root.color

            Row {
                id: tail
                spacing: fm.averageCharacterWidth * 0.6
                QQC2.Label { id: dots; text: "…"; font: edit.font; color: edit.color }
                QQC2.Label {
                    text: root.moreText
                    font.family: edit.font.family
                    font.pointSize: edit.font.pointSize
                    font.underline: moreArea.containsMouse
                    color: root.linkColor
                    MouseArea {
                        id: moreArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.expanded = true
                    }
                }
            }
        }

        QQC2.Label {
            id: less
            visible: root.expanded && root.collapsible
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: root.lessText
            font.underline: lessArea.containsMouse
            color: root.linkColor
            MouseArea {
                id: lessArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expanded = false
            }
        }
    }
}
