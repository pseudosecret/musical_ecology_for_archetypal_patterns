import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

ColumnLayout {
    id: preview
    required property var workspace
    spacing: 6
    Label { objectName: "previewTitle"; text: "Interactive preview"; font.bold: true; Layout.fillWidth: true }
    Label { text: "Every adjustment starts from the captured selection."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    RowLayout {
        CheckBox {
            objectName: "previewAutoAudition"
            text: "Auto-audition"
            checked: workspace.previewAudition
            onToggled: workspace.previewAudition = checked
            Accessible.description: "Play the updated passage once after each adjustment settles"
        }
        ToolButton { id: keys; text: "Keys…"; checkable: true }
    }
    Button {
        objectName: "stopPreviewAudition"
        text: workspace.previewRestartPending ? "Stop pending audition" : "Stop audition"
        enabled: workspace.previewRestartPending || (workspace.currentEditor && workspace.currentEditor.playing)
        onClicked: workspace.stopPreviewAudition()
    }
    TransformKeys { workspace: preview.workspace; visible: keys.checked || !workspace.keysReady }
    TransformSteps {
        id: controls
        workspace: preview.workspace
        Layout.fillWidth: true
        Layout.fillHeight: true
        enabled: workspace.previewControlsEnabled
        Component.onCompleted: load(workspace.previewSteps)
        onEdited: function(values, flush) { workspace.updatePreview(values, flush) }
        Connections { target: preview.workspace; function onPreviewStepsChanged() { controls.load(preview.workspace.previewSteps) } }
    }
    Label { text: workspace.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "Preview status" }
    RowLayout {
        Button { objectName: "resetPreview"; text: "Reset"; enabled: workspace.previewControlsEnabled; onClicked: workspace.resetPreview() }
        Button { objectName: "cancelPreview"; text: "Cancel"; onClicked: workspace.cancelPreview() }
        Button { objectName: "keepPreview"; text: "Keep result"; enabled: workspace.previewCanKeep; onClicked: workspace.keepPreview() }
    }
}
