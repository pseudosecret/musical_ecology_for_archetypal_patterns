import QtQuick 6.2
import QtQuick.Controls 6.2
import Music 1.0

Item {
    id: pane
    property var editor: null
    property bool keyboardEnabled: true
    property var previousEditor: null
    property bool ready: false
    readonly property alias view: view
    signal focused()
    function remember() {
        if (previousEditor) previousEditor.rememberScroll(scroll.contentX, scroll.contentY)
    }
    function restore() {
        previousEditor = editor
        if (!editor) return
        var pos = editor.rememberedScroll()
        scroll.contentX = Math.max(0, pos.x)
        scroll.contentY = Math.max(0, pos.y)
    }
    function reveal(rect) {
        if (rect.y < scroll.contentY) scroll.contentY = Math.max(0, rect.y - 35)
        else if (rect.y + rect.height > scroll.contentY + scroll.height)
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, rect.y + rect.height - scroll.height + 35))
        if (rect.x < scroll.contentX) scroll.contentX = Math.max(0, rect.x - 35)
        else if (rect.x + rect.width > scroll.contentX + scroll.width)
            scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, rect.x + rect.width - scroll.width + 35))
    }
    onEditorChanged: if (ready) { restore(); Qt.callLater(restore) }
    Component.onCompleted: { ready = true; restore(); Qt.callLater(restore) }
    Connections {
        target: pane.editor
        function onSelectionChanged() { if (!pane.editor.hasRangeSelection) pane.reveal(pane.editor.selectionRect) }
        function onRevealSelection() { pane.reveal(pane.editor.selectionRect) }
        function onPlayheadChanged() {
            if (pane.editor.playing) pane.reveal(Qt.rect(pane.editor.playheadX, pane.editor.playheadY, 2, Math.min(pane.editor.playheadHeight, scroll.height - 60)))
        }
    }
    Flickable {
        id: scroll
        objectName: "resultScroll"
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(width, view.width)
        contentHeight: view.height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}
        ScoreView {
            id: view
            objectName: "resultScoreView"
            editor: pane.editor
            width: pane.editor ? pane.editor.scoreWidth : 0
            height: pane.editor ? pane.editor.scoreHeight : 0
            viewportY: scroll.contentY
            viewportHeight: scroll.height
            onActiveFocusChanged: if (activeFocus) pane.focused()
            Keys.onPressed: function(event) {
                if (event.isAutoRepeat && (event.key === Qt.Key_Space || event.key === Qt.Key_P)) { event.accepted = true; return }
                if (pane.keyboardEnabled && pane.editor && pane.editor.handleKey(event.key, event.modifiers)) event.accepted = true
            }
        }
        Rectangle {
            visible: pane.editor && !pane.editor.selectionIsNote && !pane.editor.hasRangeSelection
            x: pane.editor ? pane.editor.selectionRect.x : 0
            y: pane.editor ? pane.editor.selectionRect.y : 0
            width: pane.editor ? pane.editor.selectionRect.width : 0
            height: pane.editor ? pane.editor.selectionRect.height : 0
            color: "#112c70d5"; border.color: "#2c70d5"; z: 2
        }
        Rectangle {
            visible: pane.editor && pane.editor.playing
            x: pane.editor ? pane.editor.playheadX : 0
            y: pane.editor ? pane.editor.playheadY : 0
            width: 2; height: pane.editor ? pane.editor.playheadHeight : 0
            color: "#cf453e"; z: 3
        }
    }
}
