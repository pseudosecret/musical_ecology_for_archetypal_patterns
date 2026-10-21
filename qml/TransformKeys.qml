import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

GroupBox {
    required property var workspace
    title: "Keys and scales"
    Layout.fillWidth: true
    Layout.preferredHeight: Math.min(190, 50 + workspace.keyRegions.length * 125)
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Button { objectName: "suggestKeys"; text: "Suggest missing keys"; enabled: !workspace.previewActive && !workspace.busy && !workspace.keysReady; onClicked: workspace.suggestKeys() }
            Label { text: workspace.keysReady ? "All regions have a key." : "Confirm missing keys before tonal operations."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: workspace.keyRegions
            delegate: ColumnLayout {
                id: keyRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 125
                enabled: workspace.previewActive ? workspace.previewControlsEnabled : !workspace.busy
                Label { text: keyRow.modelData.position + " · " + keyRow.modelData.label; font.bold: !keyRow.modelData.known }
                RowLayout {
                    KeyChoice { id: regionChoice; editor: workspace.sourceEditor; fifths: keyRow.modelData.fifths; form: keyRow.modelData.form; Layout.fillWidth: true }
                    Button { objectName: "confirmRegionKey" + keyRow.index; text: "Use key"; onClicked: workspace.setRegionKey(keyRow.index, regionChoice.fifths, regionChoice.form) }
                }
                ComboBox {
                    objectName: "keySuggestions" + keyRow.index
                    visible: keyRow.modelData.suggestions.length > 0
                    Layout.fillWidth: true
                    model: keyRow.modelData.suggestions
                    textRole: "label"
                    displayText: currentIndex < 0 ? "Choose a suggested key…" : currentText
                    currentIndex: -1
                    Accessible.name: "Suggested keys for region " + (keyRow.index + 1)
                    onActivated: function(option) {
                        regionChoice.fifths = keyRow.modelData.suggestions[option].fifths
                        regionChoice.form = keyRow.modelData.suggestions[option].form
                    }
                }
            }
        }
    }
}
