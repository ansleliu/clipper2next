import QtQuick
import QtQuick.Controls
import Clipper2NextDemo 1.0

SpinBox {
    id: control

    editable: true
    implicitHeight: 34
    font.family: AppTheme.fontFamily
    font.pixelSize: 13
    leftPadding: 10
    rightPadding: 34

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: AppTheme.ink
        selectionColor: AppTheme.accentSoft
        selectedTextColor: AppTheme.ink
        horizontalAlignment: TextInput.AlignLeft
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        leftPadding: 10
        rightPadding: 34
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 1
        implicitWidth: 28
        height: control.height / 2 - 1
        radius: 0
        color: control.up.pressed ? "#dce8e5" : control.up.hovered ? "#eef5f3" : "transparent"

        Text {
            text: "+"
            anchors.centerIn: parent
            color: control.up.pressed ? AppTheme.accent : AppTheme.muted
            font.family: AppTheme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: control.height / 2
        implicitWidth: 28
        height: control.height / 2 - 1
        radius: 0
        color: control.down.pressed ? "#dce8e5" : control.down.hovered ? "#eef5f3" : "transparent"

        Text {
            text: "-"
            anchors.centerIn: parent
            color: control.down.pressed ? AppTheme.accent : AppTheme.muted
            font.family: AppTheme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    background: Rectangle {
        radius: AppTheme.controlRadius
        color: AppTheme.surface
        border.color: control.activeFocus ? AppTheme.accent : AppTheme.line
        border.width: 1

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 29
            radius: AppTheme.controlRadius
            color: "#f4f7f8"
            border.color: "transparent"
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: AppTheme.lineSoft
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 1
            anchors.verticalCenter: parent.verticalCenter
            width: 27
            height: 1
            color: AppTheme.lineSoft
        }
    }
}
