import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2
import QtQuick.Dialogs 6.2
import Music 1.0

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 780
    minimumWidth: 760
    minimumHeight: 500
    title: (appEditor.dirty ? "● " : "") + appEditor.title + " — Music as Becoming"
    readonly property var workspace: appEditor.transformations
    property var propertyEditor: appEditor
    property var saveEditor: appEditor
    property var focusedEditor: appEditor
    readonly property bool previewPresented: workspaceVisible && workspace.currentIsPreview && (!workspace.floating || resultWindow.visibility !== Window.Minimized)
    onPreviewPresentedChanged: workspace.setPreviewVisible(previewPresented)
    property bool workspaceVisible: false
    onWorkspaceVisibleChanged: if (!workspaceVisible) { focusedEditor = appEditor; Qt.callLater(function() { scoreView.forceActiveFocus() }) }
    property int pendingResultClose: -1
    property int closeAfterSave: -1
    property int pendingAction: 0
    property bool closingAfterConfirmation: false
    property var selectedParts: []
    readonly property bool propertiesOpen: staffDialog.visible || keyDialog.visible || meterDialog.visible || tempoDialog.visible || instrumentationDialog.visible || unsavedPopup.visible || transformDialog.visible || resultCloseDialog.visible

    function staffInstrumentOptions() {
        var result = ["Use part instrument"]
        for (var i = 0; i < appEditor.instruments.length; ++i) result.push(appEditor.instruments[i])
        return result
    }
    function restoreScoreFocus() {
        // Popups restore their previous focus after closing; restore score focus after that.
        Qt.callLater(function() {
            if (!root.propertiesOpen && !openDialog.visible && !saveDialog.visible)
                if (root.focusedEditor && root.focusedEditor.scratch && root.workspaceVisible) root.focusResult()
                else scoreView.forceActiveFocus()
        })
    }
    function focusResult() {
        if (workspace.floating && resultWindow.visible) resultWindow.requestActivate()
        var loader = workspace.floating ? floatingResults : dockedResults
        if (loader.item) loader.item.view.forceActiveFocus()
    }
    function hideResults() {
        workspace.rememberPresentation()
        workspaceVisible = false
        root.requestActivate()
    }
    function restoreResults() {
        if (workspace.tabs.length === 0) return
        workspaceVisible = true
        focusedEditor = workspace.currentEditor
        if (workspace.floating) resultWindow.showNormal()
        Qt.callLater(function() {
            if (!root.workspaceVisible) return
            if (workspace.floating) { resultWindow.raise(); resultWindow.requestActivate() }
            root.focusResult()
        })
    }
    function showProperties(editor, kind) {
        propertyEditor = editor
        focusedEditor = editor
        if (editor.scratch && workspace.floating && resultWindow.visible) resultWindow.requestActivate()
        else root.requestActivate()
        if (kind === 0) staffDialog.open()
        else if (kind === 1) meterDialog.open()
        else if (kind === 3) keyDialog.open()
        else tempoDialog.open()
    }
    function saveResult(editor) { saveEditor = editor; saveDialog.open() }
    function finishResultClose() {
        workspace.closeResult(pendingResultClose, true)
        pendingResultClose = -1
        if (pendingAction === 3) root.close()
    }
    function requestAction(action) {
        if (appEditor.fileBusy) return
        if (appEditor.dirty) { pendingAction = action; unsavedPopup.open() }
        else performAction(action)
    }
    function performAction(action) {
        if (action === 1) appEditor.newScore()
        else if (action === 2) openDialog.open()
        else if (action === 3) { closingAfterConfirmation = true; root.close() }
        scoreView.forceActiveFocus()
    }
    function reveal(rect) {
        if (rect.y < scroll.contentY) scroll.contentY = Math.max(0, rect.y - 35)
        else if (rect.y + rect.height > scroll.contentY + scroll.height)
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, rect.y + rect.height - scroll.height + 35))
        if (rect.x < scroll.contentX) scroll.contentX = Math.max(0, rect.x - 35)
        else if (rect.x + rect.width > scroll.contentX + scroll.width)
            scroll.contentX = Math.max(0, Math.min(scroll.contentWidth - scroll.width, rect.x + rect.width - scroll.width + 35))
    }
    onClosing: function(close) {
        if (appEditor.fileBusy || workspace.anyFileBusy()) close.accepted = false
        else if (appEditor.dirty && !closingAfterConfirmation) { close.accepted = false; requestAction(3) }
        else if (workspace.firstDirty() >= 0) {
            close.accepted = false
            pendingAction = 3
            workspace.closeResult(workspace.firstDirty())
        }
        if (close.accepted) workspace.cancelPreview()
    }
    Shortcut {
        sequence: "Esc"
        enabled: !root.propertiesOpen && !openDialog.visible && !saveDialog.visible
        onActivated: (root.focusedEditor || appEditor).handleKey(Qt.Key_Escape, Qt.NoModifier)
    }
    Shortcut {
        sequence: "Space"
        autoRepeat: false
        enabled: !root.propertiesOpen && !openDialog.visible && !saveDialog.visible && !appEditor.fileBusy && !(restoreResultsTab.visible && restoreResultsTab.activeFocus)
        onActivated: (root.focusedEditor || appEditor).play(true)
    }
    Connections {
        target: appEditor
        function onNewRequested() { root.requestAction(1) }
        function onOpenRequested() { root.requestAction(2) }
        function onSaveAsRequested() { root.saveEditor = appEditor; saveDialog.open() }
        function onStaffPropertiesRequested() { root.showProperties(appEditor, 0) }
        function onKeyPropertiesRequested() { root.showProperties(appEditor, 3) }
        function onMeterPropertiesRequested() { root.showProperties(appEditor, 1) }
        function onTempoPropertiesRequested() { root.showProperties(appEditor, 2) }
        function onSelectionChanged() { if (!appEditor.hasRangeSelection) root.reveal(appEditor.selectionRect) }
        function onRevealSelection() { root.reveal(appEditor.selectionRect) }
        function onPlayheadChanged() { if (appEditor.playing) root.reveal(Qt.rect(appEditor.playheadX, appEditor.playheadY, 2, Math.min(appEditor.playheadHeight, scroll.height - 60))) }
    }
    FileDialog {
        id: openDialog
        title: "Open MusicXML"
        fileMode: FileDialog.OpenFile
        nameFilters: ["MusicXML files (*.musicxml *.xml)"]
        onAccepted: { appEditor.openFile(selectedFile); scoreView.forceActiveFocus() }
        onRejected: scoreView.forceActiveFocus()
    }
    FileDialog {
        id: saveDialog
        title: "Save MusicXML"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "musicxml"
        nameFilters: ["MusicXML files (*.musicxml)"]
        onAccepted: { root.saveEditor.saveAs(selectedFile); root.restoreScoreFocus() }
        onRejected: { root.closeAfterSave = -1; root.restoreScoreFocus() }
    }
    Dialog {
        id: unsavedPopup
        anchors.centerIn: parent
        title: "Discard unsaved changes?"
        modal: true
        focus: true
        width: 370
        standardButtons: Dialog.Cancel | Dialog.Discard
        Label { text: "Save the score first if you want to keep it."; wrapMode: Text.WordWrap; width: parent.width }
        onDiscarded: root.performAction(root.pendingAction)
        onClosed: root.restoreScoreFocus()
    }
    Dialog {
        id: staffDialog
        parent: root.propertyEditor && root.propertyEditor.scratch && workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        objectName: "staffDialog"
        anchors.centerIn: parent
        title: "Staff clef"
        modal: true
        focus: true
        width: 420
        contentWidth: availableWidth
        onOpened: clefChoice.currentIndex = root.propertyEditor.selectedClef
        onClosed: root.restoreScoreFocus()
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: "Start at the selected note, or the beginning of the selected measure."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox { id: clefChoice; objectName: "clefChoice"; model: root.propertyEditor.clefs; Layout.fillWidth: true; Accessible.name: "Clef" }
            Label { text: "Existing notes keep their sounding pitches."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "Remove change"; objectName: "removeClef"; enabled: root.propertyEditor.canRemoveClef; onClicked: { if (root.propertyEditor.removeClef()) staffDialog.close() } }
                Button { text: "Cancel"; onClicked: staffDialog.close() }
                Button { text: "Apply"; objectName: "applyClef"; onClicked: { if (root.propertyEditor.applyClef(clefChoice.currentIndex)) staffDialog.close() } }
            }
        }
    }
    Dialog {
        id: keyDialog
        objectName: "keyDialog"
        parent: root.propertyEditor && root.propertyEditor.scratch && workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        anchors.centerIn: parent
        title: "Key and scale — selected staff"
        modal: true; focus: true
        width: Math.min(parent.width - 40, 700)
        contentWidth: availableWidth
        contentHeight: 200
        onOpened: {
            keyChoice.form = root.propertyEditor.selectedKey.form
            keyChoice.fifths = root.propertyEditor.selectedKey.fifths
        }
        onClosed: root.restoreScoreFocus()
        contentItem: ColumnLayout {
            Label { text: "Start at the selected note, rest, or measure. Existing sounding pitches are retained."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            KeyChoice { id: keyChoice; editor: root.propertyEditor; Layout.fillWidth: true }
            Label { text: "Melodic minor uses raised 6 and 7 in both directions. The printed signature remains the conventional minor signature."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "Remove change"; objectName: "removeKey"; enabled: root.propertyEditor.selectedKey.removable; onClicked: { if (root.propertyEditor.removeKey()) keyDialog.close() } }
                Button { text: "Cancel"; onClicked: keyDialog.close() }
                Button { text: "Apply"; objectName: "applyKey"; onClicked: { if (root.propertyEditor.applyKey(keyChoice.fifths, keyChoice.form)) keyDialog.close() } }
            }
        }
    }
    Dialog {
        id: meterDialog
        parent: root.propertyEditor && root.propertyEditor.scratch && workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        objectName: "meterDialog"
        anchors.centerIn: parent
        title: "Time signature — measure " + (root.propertyEditor.selectedMeasure + 1)
        modal: true
        focus: true
        width: 460
        contentWidth: availableWidth
        onOpened: {
            numerator.value = root.propertyEditor.selectedMeter.numerator
            denominator.currentIndex = [2, 4, 8, 16, 32].indexOf(root.propertyEditor.selectedMeter.denominator)
            beatGroups.text = root.propertyEditor.selectedMeter.groups
        }
        onClosed: root.restoreScoreFocus()
        contentItem: Column {
            spacing: 12
            RowLayout {
                width: parent.width
                Label { text: "Numerator" }
                SpinBox { id: numerator; from: 1; to: 32; editable: true; onValueModified: beatGroups.text = "" }
                Label { text: "Denominator" }
                ComboBox { id: denominator; model: [2, 4, 8, 16, 32]; onActivated: beatGroups.text = "" }
            }
            Label { text: "Beat groups (for example 2+2+3 in 7/8)"; width: parent.width }
            TextField { id: beatGroups; width: parent.width; placeholderText: "Automatic grouping"; Accessible.name: "Beat groups" }
            Label { text: "Applies to every staff from this barline until the next meter change. Notes spill into new measures and tie across new barlines."; wrapMode: Text.WordWrap; width: parent.width }
            Label { text: root.propertyEditor.status; wrapMode: Text.WordWrap; width: parent.width }
            RowLayout {
                width: parent.width
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: meterDialog.close() }
                Button { text: "Apply"; onClicked: { if (root.propertyEditor.applyMeter(numerator.value, [2,4,8,16,32][denominator.currentIndex], beatGroups.text)) meterDialog.close() } }
            }
        }
    }
    Dialog {
        id: tempoDialog
        parent: root.propertyEditor && root.propertyEditor.scratch && workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        objectName: "tempoDialog"
        anchors.centerIn: parent
        title: "Tempo — measure " + (root.propertyEditor.selectedMeasure + 1)
        modal: true
        focus: true
        width: 450
        contentWidth: availableWidth
        onOpened: {
            tempoUnit.currentIndex = root.propertyEditor.tempoBeatValues.indexOf(root.propertyEditor.selectedTempo.beat)
            tempoDot.checked = root.propertyEditor.selectedTempo.dotted
            bpm.value = root.propertyEditor.selectedTempo.bpm
        }
        onClosed: root.restoreScoreFocus()
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                ComboBox { id: tempoUnit; model: ["Whole", "Half", "Quarter", "Eighth", "16th"]; Layout.fillWidth: true; Accessible.name: "Tempo beat unit" }
                CheckBox { id: tempoDot; text: "Dotted" }
            }
            RowLayout { Label { text: "Beats per minute" } SpinBox { id: bpm; objectName: "tempoBpm"; from: 20; to: 400; editable: true } }
            Label { text: "Begins at this measure and controls playback across all staves."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Button { text: "Remove mark"; enabled: root.propertyEditor.selectedMeasure > 0 && root.propertyEditor.selectedTempo.explicit; onClicked: { if (root.propertyEditor.removeTempo()) tempoDialog.close() } }
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: tempoDialog.close() }
                Button { text: "Apply"; objectName: "applyTempo"; onClicked: { if (root.propertyEditor.applyTempo(root.propertyEditor.tempoBeatValues[tempoUnit.currentIndex], bpm.value, tempoDot.checked)) tempoDialog.close() } }
            }
        }
    }
    Dialog {
        id: instrumentationDialog
        objectName: "instrumentationDialog"
        anchors.centerIn: parent
        title: "Instrumentation"
        modal: true
        focus: true
        width: Math.min(root.width - 40, 980)
        height: Math.min(root.height - 40, 670)
        standardButtons: Dialog.Close
        onClosed: root.restoreScoreFocus()
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                ComboBox { id: newInstrument; model: appEditor.instruments; Layout.fillWidth: true; Accessible.name: "New part instrument" }
                Button { text: "Test sound"; enabled: appEditor.audioReady; onClicked: appEditor.testInstrument(newInstrument.currentIndex) }
                Button { text: "Add part"; onClicked: appEditor.addPart(newInstrument.currentIndex, false) }
                Button { text: "Add piano (2 staves)"; onClicked: appEditor.addPart(0, true) }
            }
            TabBar { id: instrumentTabs; Layout.fillWidth: true; TabButton { text: "Staves" } TabButton { text: "Parts & groups" } }
            StackLayout {
                currentIndex: instrumentTabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    clip: true
                    model: appEditor.staffRows
                    spacing: 8
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Frame {
                        width: ListView.view.width
                        height: 105
                        required property var modelData
                        ColumnLayout {
                            anchors.fill: parent
                            RowLayout {
                                Label { text: modelData.partName + " — staff " + (modelData.index + 1); Layout.fillWidth: true }
                                Button { text: "↑"; Accessible.name: "Move staff up"; onClicked: appEditor.moveStaff(modelData.id, -1) }
                                Button { text: "↓"; Accessible.name: "Move staff down"; onClicked: appEditor.moveStaff(modelData.id, 1) }
                                Button { text: "Add staff"; onClicked: appEditor.addStaff(modelData.part, 0) }
                                Button { text: "Remove"; enabled: appEditor.staffRows.length > 1; onClicked: appEditor.removeStaff(modelData.id) }
                            }
                            RowLayout {
                                TextField { id: staffName; text: modelData.name; Layout.preferredWidth: 190; Accessible.name: "Staff name" }
                                ComboBox {
                                    id: staffInstrument; objectName: "staffInstrument"
                                    model: root.staffInstrumentOptions(); currentIndex: modelData.program + 1
                                    Layout.fillWidth: true; Accessible.name: "Staff MIDI instrument"
                                    onActivated: appEditor.configureStaff(modelData.id, staffName.text, currentIndex - 1)
                                }
                                Button { text: "Test"; enabled: appEditor.audioReady; onClicked: appEditor.testInstrument(staffInstrument.currentIndex === 0 ? modelData.effectiveProgram : staffInstrument.currentIndex - 1) }
                                Button { text: "Rename"; objectName: "applyStaff"; onClicked: appEditor.configureStaff(modelData.id, staffName.text, staffInstrument.currentIndex - 1) }
                                Button { text: "Clef…"; onClicked: { appEditor.selectMeasure(modelData.index, appEditor.selectedMeasure); appEditor.requestStaffProperties() } }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Label { text: "Select adjacent parts to create an ensemble bracket. A part's multiple staves share a brace."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: appEditor.partRows
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Frame {
                            width: ListView.view.width
                            height: 78
                            required property var modelData
                            RowLayout {
                                anchors.fill: parent
                                CheckBox {
                                    checked: root.selectedParts.indexOf(modelData.id) >= 0
                                    Accessible.name: "Select " + modelData.name + " for grouping"
                                    onToggled: {
                                        var list = root.selectedParts.slice()
                                        var i = list.indexOf(modelData.id)
                                        if (checked && i < 0) list.push(modelData.id)
                                        else if (!checked && i >= 0) list.splice(i, 1)
                                        root.selectedParts = list
                                    }
                                }
                                TextField { id: partName; text: modelData.name; Layout.preferredWidth: 190; Accessible.name: "Part name" }
                                ComboBox {
                                    id: partInstrument; objectName: "partInstrument"
                                    model: appEditor.instruments; currentIndex: modelData.program
                                    Layout.fillWidth: true; Accessible.name: "Part instrument"
                                    onActivated: appEditor.configurePart(modelData.id, partName.text, currentIndex)
                                }
                                Button { text: "Rename"; onClicked: appEditor.configurePart(modelData.id, partName.text, partInstrument.currentIndex) }
                                ComboBox { id: newStaffClef; model: appEditor.clefs; Layout.preferredWidth: 130; Accessible.name: "Added staff clef" }
                                Button { text: "Add staff"; onClicked: appEditor.addStaff(modelData.id, newStaffClef.currentIndex) }
                            }
                        }
                    }
                    RowLayout {
                        TextField { id: groupName; placeholderText: "Group name, e.g. Strings"; Layout.fillWidth: true }
                        Button { text: "Group selected parts"; enabled: root.selectedParts.length >= 2; onClicked: { if (appEditor.groupParts(groupName.text, root.selectedParts)) root.selectedParts = [] } }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: appEditor.groupRows
                            Button { required property var modelData; text: "Ungroup " + modelData.name; onClicked: appEditor.removeGroup(modelData.id) }
                        }
                    }
                }
            }
            Label { text: appEditor.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    Connections {
        target: workspace
        function onResultClosing(editor) {
            if (root.focusedEditor === editor) root.focusedEditor = appEditor
            if (root.propertyEditor === editor) root.propertyEditor = appEditor
            if (root.saveEditor === editor) root.saveEditor = appEditor
        }
        function onConfigurationRequested() {
            root.focusedEditor = workspace.sourceEditor
            if (workspace.sourceEditor && workspace.sourceEditor.scratch && workspace.floating) resultWindow.requestActivate()
            else root.requestActivate()
            transformDialog.open()
        }
        function onResultReady() { root.restoreResults() }
        function onCloseConfirmationRequired(index) { root.pendingResultClose = index; if (workspace.floating && resultWindow.visible) resultWindow.requestActivate(); else root.requestActivate(); resultCloseDialog.open() }
    }
    Connections {
        target: root.saveEditor
        function onStateChanged() {
            if (root.closeAfterSave >= 0 && !root.saveEditor.fileBusy && !root.saveEditor.dirty) {
                root.pendingResultClose = root.closeAfterSave
                root.closeAfterSave = -1
                root.finishResultClose()
            }
        }
    }
    TransformDialog {
        id: transformDialog
        workspace: root.workspace
        parent: workspace.sourceEditor && workspace.sourceEditor.scratch && workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        anchors.centerIn: parent
        onClosed: { if (!workspace.previewActive) workspace.cancel(); root.restoreScoreFocus() }
    }
    Dialog {
        id: resultCloseDialog
        parent: workspace.floating && resultWindow.visible ? resultWindow.contentItem : root.contentItem
        objectName: "resultCloseDialog"
        title: "Keep scratch-score edits?"
        modal: true; focus: true
        anchors.centerIn: parent
        width: 440
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            Label { text: "This result has manual edits. Save it explicitly, or discard it."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Button { text: "Cancel"; onClicked: { root.pendingAction = 0; root.closingAfterConfirmation = false; resultCloseDialog.close() } }
                Button { text: "Discard"; objectName: "discardResult"; onClicked: { resultCloseDialog.close(); root.finishResultClose() } }
                Button { text: "Save as MusicXML…"; onClicked: { workspace.currentIndex = root.pendingResultClose; root.closeAfterSave = root.pendingResultClose; resultCloseDialog.close(); root.saveResult(workspace.currentEditor) } }
            }
        }
        onClosed: root.restoreScoreFocus()
    }
    Component {
        id: resultComponent
        ResultPane {
            workspace: root.workspace
            keyboardEnabled: !root.propertiesOpen && !openDialog.visible && !saveDialog.visible
            enabled: keyboardEnabled
            onFocused: function(editor) { root.focusedEditor = editor }
            onSaveRequested: function(editor) { root.saveResult(editor) }
            onPropertiesRequested: function(editor, kind) { root.showProperties(editor, kind) }
            onToggleDock: { workspace.floating = !workspace.floating; Qt.callLater(root.focusResult) }
        }
    }
    Window {
        id: resultWindow
        objectName: "resultWindow"
        title: "Transformation results — Music as Becoming"
        width: 1100; height: 560
        minimumWidth: 760; minimumHeight: 340
        transientParent: root
        modality: Qt.NonModal
        visible: root.workspaceVisible && workspace.floating && workspace.tabs.length > 0
        color: "white"
        onVisibilityChanged: function(visibility) {
            // Finish the native minimize transition before hiding and unloading the view.
            if (visibility === Window.Minimized) Qt.callLater(function() {
                if (resultWindow.visibility === Window.Minimized) root.hideResults()
            })
        }
        onClosing: function(close) { close.accepted = false; root.hideResults() }
        Loader { id: floatingResults; anchors.fill: parent; active: resultWindow.visible; sourceComponent: resultComponent }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 3
                ToolButton { text: "New"; enabled: !appEditor.playbackBusy && !appEditor.fileBusy; onClicked: root.requestAction(1) }
                ToolButton { text: "Open"; enabled: !appEditor.playbackBusy && !appEditor.fileBusy; onClicked: root.requestAction(2) }
                ToolButton { text: "Save"; enabled: !appEditor.fileBusy; onClicked: { appEditor.save(); scoreView.forceActiveFocus() } }
                ToolButton { text: "Save As"; enabled: !appEditor.fileBusy; onClicked: { root.saveEditor = appEditor; saveDialog.open() } }
                ToolSeparator {}
                ToolButton { text: "Undo"; enabled: appEditor.canUndo && !appEditor.playbackBusy && !appEditor.fileBusy; onClicked: { appEditor.undo(); scoreView.forceActiveFocus() } }
                ToolButton { text: "Redo"; enabled: appEditor.canRedo && !appEditor.playbackBusy && !appEditor.fileBusy; onClicked: { appEditor.redo(); scoreView.forceActiveFocus() } }
                ToolSeparator {}
                ToolButton { objectName: "playButton"; text: appEditor.playing ? "Stop" : "Play"; enabled: !appEditor.fileBusy && (appEditor.playing || appEditor.audioReady); onClicked: { appEditor.play(true); scoreView.forceActiveFocus() } }
                ToolButton { objectName: "instrumentationButton"; text: "Instrumentation…"; enabled: !appEditor.playbackBusy && !appEditor.fileBusy; onClicked: instrumentationDialog.open() }
                ToolButton { objectName: "keyButton"; text: "Key / scale…"; enabled: !appEditor.playbackBusy && !appEditor.fileBusy && !appEditor.hasRangeSelection; onClicked: appEditor.requestKeyProperties(); ToolTip.visible: hovered; ToolTip.text: appEditor.selectedKey.label }
                ToolButton { objectName: "showResults"; text: "Results"; enabled: workspace.tabs.length > 0; onClicked: root.workspaceVisible ? root.hideResults() : root.restoreResults() }
                CheckBox { text: "Audition"; checked: appEditor.auditionEnabled; onToggled: appEditor.auditionEnabled = checked }
                Label { text: "Volume" }
                Slider { objectName: "volumeSlider"; from: 0; to: 100; value: appEditor.volume; Layout.preferredWidth: 100; Accessible.name: "Playback and preview volume"; onMoved: appEditor.volume = Math.round(value) }
                Item { Layout.fillWidth: true }
            }
        }
        SplitView {
            orientation: Qt.Vertical
            Layout.fillWidth: true
            Layout.fillHeight: true
        Flickable {
            id: scroll
            SplitView.fillHeight: true
            SplitView.minimumHeight: 150
            clip: true
            contentWidth: Math.max(width, scoreView.width)
            contentHeight: scoreView.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            ScoreView {
                id: scoreView
                objectName: "scoreView"
                editor: appEditor
                width: appEditor.scoreWidth
                height: appEditor.scoreHeight
                viewportY: scroll.contentY
                viewportHeight: scroll.height
                focus: true
                onActiveFocusChanged: if (activeFocus) root.focusedEditor = appEditor
                Keys.onPressed: function(event) {
                    if (event.isAutoRepeat && (event.key === Qt.Key_Space || event.key === Qt.Key_P)) {
                        event.accepted = true
                        return
                    }
                    if (!root.propertiesOpen && !openDialog.visible && !saveDialog.visible && appEditor.handleKey(event.key, event.modifiers))
                        event.accepted = true
                }
            }
            Rectangle {
                objectName: "measureSelection"
                visible: !appEditor.selectionIsNote && !appEditor.hasRangeSelection
                x: appEditor.selectionRect.x
                y: appEditor.selectionRect.y
                width: appEditor.selectionRect.width
                height: appEditor.selectionRect.height
                color: "#112c70d5"
                border.color: "#2c70d5"
                border.width: 1
                opacity: appEditor.playing ? 0.3 : 1
                z: 2
            }
            Rectangle { visible: appEditor.playing; x: appEditor.playheadX; y: appEditor.playheadY; width: 2; height: appEditor.playheadHeight; color: "#cf453e"; z: 3 }
        }
            Loader {
                id: dockedResults
                objectName: "dockedResults"
                active: root.workspaceVisible && !workspace.floating && workspace.tabs.length > 0
                visible: active
                SplitView.preferredHeight: workspace.currentIsPreview ? 480 : 340
                SplitView.minimumHeight: active ? (workspace.currentIsPreview ? 340 : 180) : 0
                sourceComponent: resultComponent
            }
        }
        TabBar {
            visible: !root.workspaceVisible && workspace.tabs.length > 0
            Layout.alignment: Qt.AlignLeft
            TabButton {
                id: restoreResultsTab
                objectName: "restoreResultsTab"
                text: "Transform results (" + workspace.tabs.length + ")"
                width: implicitWidth
                focusPolicy: Qt.StrongFocus
                Accessible.name: "Restore transformation results"
                ToolTip.visible: hovered
                ToolTip.text: "Restore the existing results window"
                onClicked: root.restoreResults()
            }
        }
        Label { Layout.fillWidth: true; padding: 6; text: "A–G notes · Shift+←/→ range · Ctrl-click staves · Ctrl+C/V copy/paste · T transform · Shift+T tempo · 3/5/7 tuplets · [ ] duration · Space play/stop"; elide: Text.ElideRight; color: "#526174" }
        Label { Layout.fillWidth: true; padding: 7; text: appEditor.status; elide: Text.ElideRight; color: "#26374a"; background: Rectangle { color: "#ecf1f6" } }
    }
}
