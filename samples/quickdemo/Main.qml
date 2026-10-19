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

    header: ToolBar {
        objectName: "mainToolBar"
        RowLayout {
            anchors.fill: parent
            ToolButton {
                objectName: "actionButton"
                text: "Action"
            }
            ToolButton {
                objectName: "settingsButton"
                text: "Settings"
            }
            Label {
                objectName: "titleLabel"
                text: "Demo toolbar"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
        }
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

        TextField {
            objectName: "nameField"
            placeholderText: "Type something here"
            Layout.fillWidth: true
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
                objectName: "listItem" + index
                width: ListView.view.width
                text: "List item " + (index + 1)
            }
        }
    }
}
