import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

Rectangle {
    id: root

    required property var controller

    implicitHeight: 74
    radius: AppTheme.radius
    color: AppTheme.surface
    border.color: AppTheme.line
    border.width: 1

    function shortNumber(value) {
        if (Math.abs(value) >= 1000000)
            return (value / 1000000.0).toFixed(2) + "M"
        if (Math.abs(value) >= 1000)
            return (value / 1000.0).toFixed(1) + "k"
        return value.toFixed(1)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        MetricTile {
            label: "Algorithm"
            value: root.controller.algorithmMs.toFixed(3) + " ms"
            accentColor: AppTheme.accent
        }

        MetricTile {
            label: "Render"
            value: root.controller.renderMs.toFixed(1) + " ms"
            accentColor: AppTheme.subject
        }

        MetricTile {
            label: "Contours"
            value: root.controller.outputPathCount.toString()
            accentColor: AppTheme.result
        }

        MetricTile {
            label: "Output Points"
            value: root.controller.outputPointCount.toString()
            accentColor: AppTheme.clip
        }

        MetricTile {
            label: "Area"
            value: shortNumber(root.controller.outputArea)
            accentColor: AppTheme.warning
        }

        MetricTile {
            label: "Seed"
            value: root.controller.seed.toString()
            accentColor: AppTheme.faint
        }
    }
}
