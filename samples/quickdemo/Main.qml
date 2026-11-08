pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 680
    height: 520
    visible: true
    title: "QuickDemo — QtLiveInspector target"

    property int clickCount: 0
    property string lastAction: "none"

    header: ToolBar {
        objectName: "mainToolBar"
        RowLayout {
            anchors.fill: parent
            ToolButton {
                objectName: "actionButton"
                text: "Action"
                onClicked: actionPopover.open()
                Popup {
                    id: actionPopover
                    objectName: "actionPopover"
                    y: parent.height
                    padding: 12
                    background: Rectangle {
                        color: "#ffffff"
                        border.color: "#cccccc"
                        radius: 6
                    }
                    ColumnLayout {
                        spacing: 8
                        Label { text: "Quick actions" }
                        Button {
                            objectName: "popoverAction1"
                            text: "Action 1"
                            onClicked: {
                                window.lastAction = "popover:1"
                                actionPopover.close()
                            }
                        }
                        Button {
                            objectName: "popoverAction2"
                            text: "Action 2"
                            onClicked: {
                                window.lastAction = "popover:2"
                                actionPopover.close()
                            }
                        }
                    }
                }
            }
            ToolButton {
                objectName: "settingsButton"
                text: "Settings"
                onClicked: settingsDialog.open()
            }
            Label {
                objectName: "titleLabel"
                text: "Demo toolbar"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Dialog {
        id: settingsDialog
        objectName: "settingsDialog"
        title: "Settings"
        anchors.centerIn: parent
        modal: true
        width: 320

        ColumnLayout {
            spacing: 12
            width: parent.width
            Label { text: "Dialog name:" }
            TextField {
                objectName: "dialogNameField"
                placeholderText: "Type inside the dialog"
                Layout.fillWidth: true
            }
            Switch {
                objectName: "dialogSwitch"
                text: "Extra feature"
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    objectName: "dialogOkButton"
                    text: "OK"
                    onClicked: settingsDialog.accept()
                }
                Button {
                    objectName: "dialogCancelButton"
                    text: "Cancel"
                    onClicked: settingsDialog.reject()
                }
            }
        }
        onAccepted: window.lastAction = "dialog:accepted"
        onRejected: window.lastAction = "dialog:rejected"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            objectName: "buttonRow"
            spacing: 12
            Button {
                objectName: "clickButton"
                text: "Clicked %1 times".arg(window.clickCount)
                onClicked: window.clickCount++
            }
            Button {
                objectName: "resetButton"
                text: "Reset"
                onClicked: window.clickCount = 0
            }
            Switch {
                id: switchControl
                objectName: "featureSwitch"
                text: "Feature enabled"
            }
        }

        RowLayout {
            spacing: 12
            Label { text: "Volume:" }
            Slider {
                id: volumeSlider
                objectName: "volumeSlider"
                from: 0
                to: 100
                value: 40
                Layout.fillWidth: true
            }
            Label {
                objectName: "volumeLabel"
                text: Math.round(volumeSlider.value)
            }
        }

        RowLayout {
            spacing: 12
            Label { text: "Fruit:" }
            ComboBox {
                id: fruitCombo
                objectName: "fruitCombo"
                model: ["Apple", "Banana", "Cherry", "Dragonfruit"]
                Layout.fillWidth: true
                onActivated: window.lastAction = "combo:" + currentText
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    objectName: "comboItem" + index
                    text: modelData
                    width: fruitCombo.width
                }
            }
        }

        TextField {
            objectName: "nameField"
            placeholderText: "Type something here"
            Layout.fillWidth: true
        }

        Label {
            objectName: "statusLabel"
            text: "last action: " + window.lastAction
        }

        Rectangle {
            objectName: "animationStage"
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "#f0f0f0"
            border.color: "#cccccc"

            Rectangle {
                id: ball
                objectName: "animatedBall"
                width: 56
                height: 56
                radius: 28
                color: switchControl.checked ? "tomato" : "steelblue"
                y: 42

                SequentialAnimation on x {
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: 20
                        to: ball.parent.width - ball.width - 20
                        duration: 2000
                        easing.type: Easing.InOutQuad
                    }
                    NumberAnimation {
                        from: ball.parent.width - ball.width - 20
                        to: 20
                        duration: 2000
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }

        ListView {
            objectName: "itemList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: 12
            delegate: ItemDelegate {
                required property int index
                objectName: "listItem" + index
                width: ListView.view.width
                text: "List item " + (index + 1)
            }
        }
    }
}
