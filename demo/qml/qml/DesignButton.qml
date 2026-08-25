import QtQuick
import QtQuick.Controls
import Clipper2NextDemo 1.0

Button {
    id: control

    property string variant: "secondary"
    property color accentColor: AppTheme.accent

    implicitHeight: 34
    implicitWidth: Math.max(72, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    topPadding: 7
    bottomPadding: 7

    font.family: AppTheme.fontFamily
    font.pixelSize: 13
    font.weight: checked || variant === "primary" ? Font.DemiBold : Font.Medium

    contentItem: Text {
        text: control.text
        font: control.font
        color: {
            if (!control.enabled)
                return AppTheme.faint
            if (control.variant === "primary")
                return "#ffffff"
            if (control.checked)
                return control.accentColor
            return AppTheme.text
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: AppTheme.controlRadius
        color: {
            if (!control.enabled)
                return "#f2f4f6"
            if (control.variant === "primary")
                return control.down ? "#0b5f59" : control.hovered ? "#13857b" : AppTheme.accent
            if (control.checked)
                return control.down ? "#ccebe5" : AppTheme.accentSoft
            if (control.variant === "ghost")
                return control.hovered || control.down ? "#edf3f4" : "transparent"
            return control.down ? "#e6edf0" : control.hovered ? "#f5f8fa" : AppTheme.surface
        }
        border.color: {
            if (control.variant === "primary")
                return color
            if (control.checked)
                return Qt.rgba(control.accentColor.r, control.accentColor.g, control.accentColor.b, 0.38)
            if (control.variant === "ghost")
                return "transparent"
            return AppTheme.line
        }
        border.width: control.variant === "ghost" ? 0 : 1

        Behavior on color {
            ColorAnimation { duration: 110 }
        }
        Behavior on border.color {
            ColorAnimation { duration: 110 }
        }
    }
}
