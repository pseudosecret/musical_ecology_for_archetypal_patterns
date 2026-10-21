import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

ColumnLayout {
    id: panel
    required property var workspace
    function review(id, decision, name) {
        var y = findings.contentY
        panel.workspace.reviewFinding(id, decision, name)
        Qt.callLater(function() { findings.contentY = Math.max(0, Math.min(y, findings.contentHeight - findings.height)) })
    }
    spacing: 6
    Label {
        Layout.fillWidth: true
        text: panel.workspace.reportStatus
        wrapMode: Text.WordWrap
        font.bold: true
        color: panel.workspace.analysisStale ? "#a04d17" : "#243c53"
    }
    Button {
        objectName: "retryPreviewReport"
        visible: panel.workspace.currentIsPreview
        text: "Refresh report"
        enabled: panel.workspace.previewCanKeep && !panel.workspace.busy
        onClicked: panel.workspace.retryPreviewReport()
    }
    RowLayout {
        enabled: panel.workspace.hasAnalysis
        Button { objectName: "copyAnalysisReport"; text: "Copy report"; onClicked: panel.workspace.copyAnalysis(false) }
        Button { objectName: "copyAnalysisData"; text: "Copy structured data"; onClicked: panel.workspace.copyAnalysis(true) }
    }
    TabBar {
        id: tabs
        Layout.fillWidth: true
        TabButton { text: "Report" }
        TabButton { objectName: "analysisFindingsTab"; text: "Findings" }
    }
    ComboBox {
        id: findingView
        objectName: "analysisFindingView"
        visible: tabs.currentIndex === 1
        Layout.fillWidth: true
        model: ["Structures", "Cells", "All findings"]
        Accessible.name: "Analysis findings view"
    }
    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex
        ScrollView {
            clip: true
            contentWidth: availableWidth
            TextArea {
                objectName: "analysisReport"
                text: panel.workspace.analysisReport
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                Accessible.name: "Copyable melody analysis report"
            }
        }
        ListView {
            id: findings
            objectName: "analysisFindingList"
            clip: true
            spacing: 6
            model: panel.workspace.analysisFindings.filter(function(f) {
                return findingView.currentIndex === 2 || (findingView.currentIndex === 1 ? f.kind === "cell" : f.kind === "motif" || f.kind === "gesture")
            })
            ScrollBar.vertical: ScrollBar {}
            Label {
                visible: findings.count === 0
                width: findings.width
                padding: 12
                text: "No candidates in this view. Try Cells or All findings."
                wrapMode: Text.WordWrap
            }
            delegate: Frame {
                id: finding
                required property var modelData
                property bool showEvidence: false
                width: ListView.view.width
                implicitHeight: content.implicitHeight + topPadding + bottomPadding
                ColumnLayout {
                    id: content
                    width: parent.width
                    Label { text: finding.modelData.title; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: finding.modelData.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label {
                        objectName: "salienceReasons" + finding.modelData.id
                        visible: text.length > 0
                        text: finding.modelData.salienceReasons.join(" · ")
                        color: "#526174"; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                    ToolButton { text: finding.showEvidence ? "Hide evidence" : "Inspect measurements"; onClicked: finding.showEvidence = !finding.showEvidence }
                    TextArea {
                        visible: finding.showEvidence
                        Layout.fillWidth: true
                        text: finding.showEvidence ? "Evidence strength: " + finding.modelData.strength.toFixed(2) +
                              (finding.modelData.salience >= 0 ? "\nStructural salience: " + finding.modelData.salience.toFixed(2) : "") +
                              "\n\n" + panel.workspace.findingEvidence(finding.modelData.id) : ""
                        textFormat: TextEdit.PlainText
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        Accessible.name: "Measurements supporting this finding"
                    }
                    RowLayout {
                        enabled: !panel.workspace.analysisStale && panel.workspace.currentEditor && !panel.workspace.currentEditor.playbackBusy
                        SpinBox {
                            id: occurrence
                            from: 1; to: finding.modelData.occurrences
                            visible: to > 1
                            Accessible.name: "Occurrence to highlight"
                        }
                        Button {
                            objectName: "showFinding" + finding.modelData.id
                            text: "Show notes"
                            onClicked: panel.workspace.highlightFinding(finding.modelData.id, occurrence.value - 1)
                        }
                    }
                    RowLayout {
                        visible: finding.modelData.reviewable
                        enabled: !panel.workspace.analysisStale && panel.workspace.currentEditor && !panel.workspace.currentEditor.playbackBusy
                        ComboBox {
                            objectName: "reviewFinding" + finding.modelData.id
                            model: ["Undecided", "Accepted", "Rejected"]
                            currentIndex: ["undecided", "accepted", "rejected"].indexOf(finding.modelData.decision)
                            Accessible.name: "Review candidate"
                            onActivated: function(index) { panel.review(finding.modelData.id, ["undecided", "accepted", "rejected"][index], finding.modelData.name) }
                        }
                        TextField {
                            id: candidateName
                            objectName: "nameFinding" + finding.modelData.id
                            Layout.fillWidth: true
                            visible: finding.modelData.kind !== "boundary"
                            text: finding.modelData.name
                            placeholderText: "Name this candidate"
                            maximumLength: 120
                            Accessible.name: "Candidate name"
                            onAccepted: panel.review(finding.modelData.id, finding.modelData.decision, text)
                        }
                        Button {
                            visible: finding.modelData.kind !== "boundary"
                            text: "Name"
                            onClicked: panel.review(finding.modelData.id, finding.modelData.decision, candidateName.text)
                        }
                    }
                }
            }
        }
    }
}
