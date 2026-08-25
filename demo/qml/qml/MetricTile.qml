import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

Rectangle {
    id: root

    property string label: ""
    property string value: ""
    property color accentColor: AppTheme.accent
    property string helper: ""

    radius: AppTheme.radius
    color: AppTheme.surfaceRaised
    border.color: AppTheme.lineSoft
    border.width: 1
    implicitHeight: 54
    Layout.fillWidth: true

    RowLayout {
        anchors.fill: parent
        anchors.margins: 11
        spacing: 10

        Rectangle {
            width: 4
            Layout.fillHeight: true
            radius: 2
            color: root.accentColor
        }

        ColumnLayout {
            spacing: 1
            Layout.fillWidth: true

            Label {
                text: root.label
                color: AppTheme.muted
                font.family: AppTheme.fontFamily
                font.pixelSize: 11
                font.weight: Font.Medium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: root.value
                color: AppTheme.ink
                font.family: AppTheme.fontFamily
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
