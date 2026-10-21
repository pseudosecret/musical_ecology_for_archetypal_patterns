import QtQuick 6.2
import QtQuick.Controls 6.2
import QtQuick.Layouts 6.2

ColumnLayout {
    id: controls
    required property var workspace
    property bool loading: false
    readonly property var ratios: [[1,4], [1,3], [1,2], [1,1], [2,1], [3,1], [4,1]]
    signal edited(var values, bool flush)
    function newStep() { return {kind: "transpose", amount: 0, numerator: 2, denominator: 1, mode: "degrees", accidentals: "preserve", axisDegree: 1, tonicOctave: workspace.defaultTonicOctave, reverseKeys: false} }
    function values() {
        var list = []
        for (var i = 0; i < steps.count; ++i) { var s = steps.get(i); list.push({kind: s.kind, amount: s.amount, numerator: s.numerator, denominator: s.denominator, mode: s.mode, accidentals: s.accidentals, axisDegree: s.axisDegree, tonicOctave: s.tonicOctave, reverseKeys: s.reverseKeys}) }
        return list
    }
    function load(list) {
        // Ordinary control changes already have this state. Keep their focus/drag.
        var current = values(), same = current.length === list.length
        for (var row = 0; same && row < current.length; ++row)
            for (var field in current[row]) if (current[row][field] !== list[row][field]) same = false
        if (same) return
        loading = true
        steps.clear()
        for (var i = 0; i < list.length; ++i) steps.append(list[i])
        loading = false
    }
    function reset() { load([newStep()]) }
    function publish(flush) { if (!loading) edited(values(), flush) }
    function ratioIndex(n, d) {
        for (var i = 0; i < ratios.length; ++i) if (n * ratios[i][1] === d * ratios[i][0]) return i
        return 3
    }
    ListModel { id: steps; objectName: "transformSteps" }
    Connections {
        target: steps
        function onDataChanged() { controls.publish(false) }
        function onRowsInserted() { controls.publish(false) }
        function onRowsRemoved() { controls.publish(false) }
        function onRowsMoved() { controls.publish(false) }
    }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: steps
            enabled: controls.enabled
            delegate: Frame {
                id: stepRow
                width: ListView.view.width
                height: kind === "invert" ? 230 : kind === "transpose" ? 190 : kind === "retrograde" ? 140 : 195
                required property int index
                required property string kind
                required property int amount
                required property int numerator
                required property int denominator
                required property string mode
                required property string accidentals
                required property int axisDegree
                required property int tonicOctave
                required property bool reverseKeys
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Label { text: (index + 1) + "." }
                        ComboBox {
                            objectName: "transformType" + index
                            Layout.fillWidth: true
                            model: ["Transposition", "Inversion", "Augmentation / diminution", "Retrograde"]
                            currentIndex: ["transpose", "invert", "scale", "retrograde"].indexOf(kind)
                            Accessible.name: "Transformation type"
                            onActivated: function(optionIndex) {
                                var next = ["transpose", "invert", "scale", "retrograde"][optionIndex]
                                steps.setProperty(stepRow.index, "kind", next)
                                steps.setProperty(stepRow.index, "amount", next === "invert" ? workspace.defaultAxis : 0)
                            }
                        }
                        ToolButton { objectName: "moveTransformUp" + index; text: "↑"; enabled: index > 0; Accessible.name: "Move step earlier"; onClicked: steps.move(index, index - 1, 1) }
                        ToolButton { text: "↓"; enabled: index + 1 < steps.count; Accessible.name: "Move step later"; onClicked: steps.move(index, index + 1, 1) }
                        ToolButton { text: "Remove"; onClicked: steps.remove(index) }
                    }
                    RowLayout {
                        visible: kind === "transpose" || kind === "invert"
                        ComboBox {
                            objectName: "transformMode" + index
                            model: ["Scale degrees", "Exact semitones"]
                            currentIndex: mode === "degrees" ? 0 : 1
                            Accessible.name: "Pitch transformation mode"
                            onActivated: function(option) {
                                steps.setProperty(stepRow.index, "mode", option === 0 ? "degrees" : "semitones")
                                if (kind === "invert") steps.setProperty(stepRow.index, "amount", workspace.defaultAxis)
                            }
                        }
                        Label { visible: kind === "transpose" || mode === "semitones"; text: kind === "transpose" ? (mode === "degrees" ? "Scale steps" : "Semitones") : "Axis MIDI pitch" }
                        SpinBox { objectName: "transformAmount" + index; visible: kind === "transpose" || mode === "semitones"; from: kind === "transpose" ? -127 : 0; to: 127; value: amount; editable: true; onValueModified: steps.setProperty(stepRow.index, "amount", value) }
                    }
                    Slider {
                        objectName: "transformSlider" + index
                        Layout.fillWidth: true
                        visible: kind === "transpose" || (kind === "invert" && mode === "semitones")
                        from: kind === "transpose" ? -24 : 0
                        to: kind === "transpose" ? 24 : 127
                        stepSize: 1; snapMode: Slider.SnapAlways
                        value: amount
                        Accessible.name: kind === "transpose" ? "Transpose amount" : "Inversion axis MIDI pitch"
                        onMoved: steps.setProperty(stepRow.index, "amount", Math.round(value))
                        onPressedChanged: if (!pressed) controls.publish(true)
                    }
                    RowLayout {
                        visible: kind === "invert" && mode === "degrees"
                        Label { text: "Local axis degree" }
                        SpinBox { from: 1; to: 7; value: axisDegree; editable: true; onValueModified: steps.setProperty(stepRow.index, "axisDegree", value); Accessible.name: "Inversion axis scale degree" }
                        Label { text: "Tonic octave" }
                        SpinBox { from: -1; to: 9; value: tonicOctave; editable: true; onValueModified: steps.setProperty(stepRow.index, "tonicOctave", value); Accessible.name: "Inversion tonic octave" }
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        visible: ((kind === "transpose" || kind === "invert") && mode === "degrees") || kind === "retrograde"
                        model: ["Preserve chromatic inflections", "Constrain to scale"]
                        currentIndex: accidentals === "preserve" ? 0 : 1
                        Accessible.name: "Chromatic accidental policy"
                        onActivated: function(option) { steps.setProperty(stepRow.index, "accidentals", option === 0 ? "preserve" : "scale") }
                    }
                    RowLayout {
                        visible: kind === "scale"
                        Label { text: "Ratio" }
                        SpinBox { Layout.fillWidth: true; from: 1; to: 1000000; value: numerator; editable: true; onValueModified: steps.setProperty(index, "numerator", value) }
                        Label { text: "/" }
                        SpinBox { Layout.fillWidth: true; from: 1; to: 1000000; value: denominator; editable: true; onValueModified: steps.setProperty(index, "denominator", value) }
                    }
                    RowLayout {
                        visible: kind === "scale"
                        Label { text: "Rhythmic ratio" }
                        Slider {
                            objectName: "durationSlider" + index
                            Layout.fillWidth: true
                            from: 0; to: 6; stepSize: 1; snapMode: Slider.SnapAlways
                            value: controls.ratioIndex(numerator, denominator)
                            Accessible.name: "Exact rhythmic ratio"
                            onMoved: {
                                var ratio = controls.ratios[Math.round(value)]
                                steps.setProperty(stepRow.index, "numerator", ratio[0])
                                steps.setProperty(stepRow.index, "denominator", ratio[1])
                            }
                            onPressedChanged: if (!pressed) controls.publish(true)
                        }
                    }
                    RowLayout {
                        visible: kind === "scale"
                        Button { text: "×2"; onClicked: { steps.setProperty(index, "numerator", 2); steps.setProperty(index, "denominator", 1) } }
                        Button { text: "×½"; onClicked: { steps.setProperty(index, "numerator", 1); steps.setProperty(index, "denominator", 2) } }
                    }
                    Label { visible: kind === "retrograde"; text: "Reverse music; remap scale degrees into the destination key." }
                    CheckBox { visible: kind === "retrograde" && workspace.keyRegions.length > 1; text: "Reverse key regions too"; checked: reverseKeys; onToggled: steps.setProperty(stepRow.index, "reverseKeys", checked) }
                }
            }
        }
    Button { objectName: "addTransformStep"; text: "Add step"; enabled: steps.count < 64; onClicked: steps.append(controls.newStep()) }
}
