import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

ColumnLayout {
    id: result
    required property var workspace
    property bool keyboardEnabled: true
    readonly property var editor: workspace.currentEditor
    readonly property alias view: score.view
    signal focused(var editor)
    signal saveRequested(var editor)
    signal propertiesRequested(var editor, int kind)
    signal toggleDock()
    spacing: 0
    Connections { target: workspace; function onPresentationChanging() { score.remember() } }
    Connections {
        target: result.editor
        function onSaveAsRequested() { result.saveRequested(result.editor) }
        function onStaffPropertiesRequested() { result.propertiesRequested(result.editor, 0) }
        function onKeyPropertiesRequested() { result.propertiesRequested(result.editor, 3) }
        function onMeterPropertiesRequested() { result.propertiesRequested(result.editor, 1) }
        function onTempoPropertiesRequested() { result.propertiesRequested(result.editor, 2) }
    }
    ToolBar {
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            ToolButton {
                objectName: "resultPlay"
                text: result.editor && result.editor.playing ? "Stop" : "Play selection"
                enabled: result.editor && !result.editor.fileBusy && (result.editor.playing || result.editor.audioReady) && (!result.editor.editLocked || !result.editor.playbackBusy || result.editor.playing)
                onClicked: { result.editor.play(true); score.view.forceActiveFocus() }
            }
            ToolButton { text: "Undo"; enabled: result.editor && result.editor.canUndo && !result.editor.playbackBusy && !result.editor.fileBusy && !result.editor.editLocked; onClicked: { result.editor.undo(); score.view.forceActiveFocus() } }
            ToolButton { text: "Redo"; enabled: result.editor && result.editor.canRedo && !result.editor.playbackBusy && !result.editor.fileBusy && !result.editor.editLocked; onClicked: { result.editor.redo(); score.view.forceActiveFocus() } }
            ToolButton { objectName: "saveResult"; text: "Save as MusicXML…"; enabled: result.editor && !result.editor.fileBusy && !result.editor.editLocked; onClicked: result.saveRequested(result.editor) }
            ToolButton { text: "Transform…"; enabled: result.editor && !result.editor.playbackBusy && !workspace.busy && !workspace.previewActive; onClicked: workspace.begin(result.editor) }
            ToolButton { text: "Key / scale…"; enabled: result.editor && !result.editor.playbackBusy && !result.editor.fileBusy && !result.editor.editLocked && !result.editor.hasRangeSelection; onClicked: result.editor.requestKeyProperties() }
            Item { Layout.fillWidth: true }
            ToolButton { objectName: "toggleDock"; text: workspace.floating ? "Dock below score" : "Float window"; onClicked: result.toggleDock() }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            contentWidth: tabRow.implicitWidth
            clip: true
            Row {
                id: tabRow
                Repeater {
                    model: workspace.tabs
                    Row {
                        required property var modelData
                        required property int index
                        Button { text: modelData.title; highlighted: index === workspace.currentIndex; onClicked: { workspace.currentIndex = index; score.view.forceActiveFocus() } }
                        ToolButton { text: "×"; Accessible.name: "Close result " + (index + 1); onClicked: workspace.closeResult(index) }
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: "Instrument"; leftPadding: 8 }
        ComboBox {
            objectName: "resultInstrument"
            Layout.fillWidth: true
            model: result.editor ? result.editor.instruments : []
            currentIndex: result.editor && result.editor.staffRows.length ? result.editor.staffRows[0].effectiveProgram : 0
            enabled: result.editor && !result.editor.playbackBusy && !result.editor.fileBusy && !result.editor.editLocked
            Accessible.name: "Result instrument"
            onActivated: result.editor.configureStaff(result.editor.staffRows[0].id, "Result", currentIndex)
        }
        Label { text: workspace.currentIsPreview ? "Keep result to edit or save" : "Temporary until explicitly saved"; rightPadding: 8; color: "#526174" }
    }
    Label { text: workspace.currentOperations; Layout.fillWidth: true; padding: 5; elide: Text.ElideRight; color: "#526174" }
    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal
        PreviewPane {
            visible: result.workspace.currentIsPreview
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 280
            workspace: result.workspace
        }
        ScorePane {
            id: score
            SplitView.fillWidth: true
            SplitView.minimumWidth: 180
            editor: result.editor
            keyboardEnabled: result.keyboardEnabled
            onFocused: result.focused(result.editor)
        }
        AnalysisPane {
            visible: result.workspace.hasAnalysis || result.workspace.currentIsPreview
            SplitView.preferredWidth: result.workspace.currentIsPreview ? 350 : 480
            SplitView.minimumWidth: 250
            workspace: result.workspace
        }
    }
    Label { text: result.editor ? result.editor.status : ""; Layout.fillWidth: true; padding: 6; elide: Text.ElideRight }
}
