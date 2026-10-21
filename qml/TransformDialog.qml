import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

Dialog {
    id: dialog
    required property var workspace
    objectName: "transformDialog"
    title: "Transform or analyze selected passage"
    modal: true
    focus: true
    width: Math.min(parent.width - 40, 900)
    height: Math.min(parent.height - 40, 760)
    contentWidth: availableWidth
    closePolicy: Popup.CloseOnEscape
    onOpened: controls.reset()
    onRejected: workspace.cancel()
    Connections { target: workspace; function onResultReady() { dialog.close() } }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: workspace.sourceSummary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: "Steps run from top to bottom. The source music stays unchanged."; Layout.fillWidth: true }
        TransformKeys { workspace: dialog.workspace }
        TransformSteps {
            id: controls
            workspace: dialog.workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            enabled: !workspace.busy
        }
        RowLayout {
            BusyIndicator { running: workspace.busy; Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
            Label { text: workspace.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        RowLayout {
            Button {
                objectName: "analyzeSelection"
                text: "Analyze selection"
                enabled: !workspace.busy && !workspace.previewActive
                onClicked: workspace.analyzeSelection()
                Accessible.description: "Analyze a monophonic melody and open an unchanged scratchpad copy with a report"
            }
            Button {
                objectName: "startPreview"
                text: "Interactive preview"
                enabled: !workspace.busy
                onClicked: workspace.beginPreview(controls.values())
            }
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: { workspace.cancel(); dialog.close() } }
            Button {
                objectName: "submitTransform"
                text: "Submit"
                enabled: !workspace.busy && controls.values().length > 0 && !workspace.previewActive
                onClicked: workspace.submit(controls.values())
            }
        }
    }
}
