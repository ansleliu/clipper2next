import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

Rectangle {
    id: root

    required property var controller
    readonly property var scenes: [
        { "label": "Clip", "value": "boolean_clip", "tone": AppTheme.subject },
        { "label": "Offset", "value": "offset", "tone": AppTheme.result },
        { "label": "Rect", "value": "rect_clip", "tone": AppTheme.clip },
        { "label": "Tri", "value": "triangulation", "tone": AppTheme.warning },
        { "label": "Batch", "value": "batch_clip", "tone": AppTheme.accent }
    ]
    property bool syncingSceneTabs: false

    function sceneIndex(value) {
        for (let index = 0; index < scenes.length; ++index) {
            if (scenes[index].value === value)
                return index
        }
        return 0
    }

    function syncSceneTabs() {
        syncingSceneTabs = true
        sceneTabs.currentIndex = sceneIndex(controller.scene)
        syncingSceneTabs = false
    }

    Component.onCompleted: syncSceneTabs()

    Connections {
        target: root.controller
        function onParametersChanged() { root.syncSceneTabs() }
    }

    Shortcut {
        sequence: "Ctrl+1"
        onActivated: root.controller.scene = "boolean_clip"
    }

    Shortcut {
        sequence: "Ctrl+2"
        onActivated: root.controller.scene = "offset"
    }

    Shortcut {
        sequence: "Ctrl+3"
        onActivated: root.controller.scene = "rect_clip"
    }

    Shortcut {
        sequence: "Ctrl+4"
        onActivated: root.controller.scene = "triangulation"
    }

    Shortcut {
        sequence: "Ctrl+5"
        onActivated: root.controller.scene = "batch_clip"
    }

    implicitHeight: 76
    color: AppTheme.window

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        spacing: 16

        ColumnLayout {
            spacing: 1
            Layout.preferredWidth: 170

            Label {
                text: "clipper2next"
                font.family: AppTheme.fontFamily
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: AppTheme.ink
            }

            Label {
                text: root.controller.executionMode
                font.family: AppTheme.fontFamily
                font.pixelSize: 11
                color: AppTheme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        TabBar {
            id: sceneTabs

            implicitHeight: 42
            Layout.preferredWidth: 500
            Layout.minimumWidth: 410
            Layout.maximumWidth: 520

            onCurrentIndexChanged: {
                if (!root.syncingSceneTabs && currentIndex >= 0)
                    root.controller.scene = root.scenes[currentIndex].value
            }

            background: Rectangle {
                radius: AppTheme.radius
                color: AppTheme.surface
                border.color: AppTheme.line
                border.width: 1
            }

            Repeater {
                model: root.scenes

                TabButton {
                    id: sceneButton

                    readonly property bool selected: TabBar.index === sceneTabs.currentIndex

                    text: modelData.label
                    width: sceneTabs.width / root.scenes.length
                    implicitHeight: 34
                    topPadding: 4
                    bottomPadding: 4
                    leftPadding: 8
                    rightPadding: 8

                    font.family: AppTheme.fontFamily
                    font.pixelSize: 13
                    font.weight: selected ? Font.DemiBold : Font.Medium

                    contentItem: Text {
                        text: sceneButton.text
                        font: sceneButton.font
                        color: sceneButton.selected ? modelData.tone : AppTheme.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: AppTheme.controlRadius
                        color: sceneButton.selected ? AppTheme.accentSoft
                              : sceneButton.hovered ? "#f4f8f8" : "transparent"
                        border.color: sceneButton.selected ? "#a6d9d1" : "transparent"
                        border.width: 1
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        DesignButton {
            text: "Run"
            variant: "primary"
            onClicked: root.controller.run()
        }

        DesignButton {
            text: "New"
            onClicked: root.controller.newSample()
        }

        DesignButton {
            text: "Fit"
            variant: "secondary"
            onClicked: root.controller.resetView()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: AppTheme.lineSoft
    }
}
