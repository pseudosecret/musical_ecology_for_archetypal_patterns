import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

RowLayout {
    id: choice
    required property var editor
    property int fifths: 0
    property int form: 0
    signal edited()
    function syncTonic() { tonic.currentIndex = choice.fifths + 7 }
    onFifthsChanged: Qt.callLater(syncTonic)
    Component.onCompleted: syncTonic()
    ComboBox {
        objectName: "keyForm"
        model: ["Major", "Minor · melodic", "Minor · harmonic (natural 6)", "Aeolian (natural minor)"]
        currentIndex: choice.form
        Accessible.name: "Scale identity and minor form"
        onActivated: function(index) { choice.form = index; choice.edited() }
    }
    ComboBox {
        id: tonic
        objectName: "keyTonic"
        Layout.fillWidth: true
        model: choice.editor ? choice.editor.keyChoices(choice.form) : []
        onModelChanged: Qt.callLater(choice.syncTonic)
        currentIndex: choice.fifths + 7
        Accessible.name: "Tonic and key signature"
        onActivated: function(index) { choice.fifths = index - 7; choice.edited() }
    }
}
