import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

Rectangle {
    id: root

    required property var controller

    color: AppTheme.surface
    radius: AppTheme.radius
    border.color: AppTheme.line
    border.width: 1

    function sceneTitle() {
        if (root.controller.scene === "offset")
            return "Offset"
        if (root.controller.scene === "rect_clip")
            return "RectClip"
        if (root.controller.scene === "triangulation")
            return "Triangulation"
        if (root.controller.scene === "batch_clip")
            return "Batch Clip"
        return "Boolean Clip"
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 1
        clip: true

        ColumnLayout {
            width: root.width - 34
            x: 17
            y: 18
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Label {
                    text: "Inspector"
                    color: AppTheme.ink
                    font.family: AppTheme.fontFamily
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Label {
                    text: sceneTitle()
                    color: AppTheme.muted
                    font.family: AppTheme.fontFamily
                    font.pixelSize: 12
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: operationContent.implicitHeight + 30
                radius: AppTheme.radius
                color: AppTheme.surfaceRaised
                border.color: AppTheme.lineSoft
                border.width: 1
                visible: root.controller.scene !== "offset"

                ColumnLayout {
                    id: operationContent
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    Label {
                        text: "Operation"
                        color: AppTheme.text
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        visible: root.controller.scene === "boolean_clip"
                                 || root.controller.scene === "batch_clip"

                        Repeater {
                            model: ["union", "intersection", "difference", "xor"]

                            Button {
                                Layout.fillWidth: true
                                implicitHeight: 34
                                text: modelData
                                checkable: true
                                checked: root.controller.operation === modelData
                                onClicked: root.controller.operation = modelData

                                font.family: AppTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: checked ? Font.DemiBold : Font.Medium

                                contentItem: Text {
                                    text: parent.text
                                    font: parent.font
                                    color: parent.checked ? AppTheme.accent : AppTheme.text
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    radius: AppTheme.controlRadius
                                    color: parent.checked ? AppTheme.accentSoft
                                                          : parent.hovered ? "#f1f5f6" : AppTheme.surface
                                    border.color: parent.checked ? "#9ed8cd" : AppTheme.line
                                    border.width: 1
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 42
                        radius: AppTheme.controlRadius
                        color: root.controller.useDelaunay ? AppTheme.accentSoft : AppTheme.surface
                        border.color: root.controller.useDelaunay ? "#9ed8cd" : AppTheme.line
                        visible: root.controller.scene === "triangulation"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12

                            Label {
                                text: "Delaunay"
                                color: AppTheme.text
                                font.family: AppTheme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }

                            Item { Layout.fillWidth: true }

                            Switch {
                                checked: root.controller.useDelaunay
                                onToggled: root.controller.useDelaunay = checked
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        visible: root.controller.scene === "rect_clip"

                        Repeater {
                            model: ["direct", "prepared", "immutable"]

                            Button {
                                Layout.fillWidth: true
                                implicitHeight: 34
                                text: modelData
                                checkable: true
                                checked: root.controller.rectClipMode === modelData
                                onClicked: root.controller.rectClipMode = modelData

                                font.family: AppTheme.fontFamily
                                font.pixelSize: 11
                                font.weight: checked ? Font.DemiBold : Font.Medium

                                contentItem: Text {
                                    text: parent.text
                                    font: parent.font
                                    color: parent.checked ? AppTheme.clip : AppTheme.text
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    radius: AppTheme.controlRadius
                                    color: parent.checked ? AppTheme.clipSoft
                                                          : parent.hovered ? "#f1f5f6" : AppTheme.surface
                                    border.color: parent.checked ? "#efb093" : AppTheme.line
                                    border.width: 1
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: inputContent.implicitHeight + 30
                radius: AppTheme.radius
                color: AppTheme.surfaceRaised
                border.color: AppTheme.lineSoft
                border.width: 1

                ColumnLayout {
                    id: inputContent
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 11

                    Label {
                        text: "Input"
                        color: AppTheme.text
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Seed"
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                    }
                    NumberBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 999999
                        value: root.controller.seed
                        onValueModified: root.controller.seed = value
                    }

                    Label {
                        text: "Paths"
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                    }
                    NumberBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 256
                        value: root.controller.pathCount
                        onValueModified: root.controller.pathCount = value
                    }

                    Label {
                        text: "Vertices"
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                    }
                    NumberBox {
                        Layout.fillWidth: true
                        from: 3
                        to: 512
                        value: root.controller.vertexCount
                        onValueModified: root.controller.vertexCount = value
                    }

                    Label {
                        text: "Offset"
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                        visible: root.controller.scene === "offset"
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: -160
                        to: 160
                        value: root.controller.offsetDelta
                        visible: root.controller.scene === "offset"
                        onMoved: root.controller.offsetDelta = value

                        background: Rectangle {
                            x: parent.leftPadding
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: parent.availableWidth
                            height: 4
                            radius: 2
                            color: AppTheme.lineSoft

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: AppTheme.result
                            }
                        }

                        handle: Rectangle {
                            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                            y: parent.topPadding + parent.availableHeight / 2 - height / 2
                            width: 16
                            height: 16
                            radius: 8
                            color: AppTheme.surface
                            border.color: AppTheme.result
                            border.width: 2
                        }
                    }
                    Label {
                        text: root.controller.offsetDelta.toFixed(1)
                        color: AppTheme.result
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        visible: root.controller.scene === "offset"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: performanceContent.implicitHeight + 30
                radius: AppTheme.radius
                color: AppTheme.surfaceRaised
                border.color: AppTheme.lineSoft
                border.width: 1

                ColumnLayout {
                    id: performanceContent
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 11

                    Label {
                        text: "Performance"
                        color: AppTheme.text
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Repeats"
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                    }
                    NumberBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 1000
                        value: root.controller.repeats
                        onValueModified: root.controller.repeats = value
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 38
                        radius: AppTheme.controlRadius
                        color: root.controller.status === "ok" ? AppTheme.resultSoft : "#fde5df"
                        border.color: root.controller.status === "ok" ? "#9bd6bd" : "#eba08f"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            spacing: 8

                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: root.controller.status === "ok" ? AppTheme.result : AppTheme.danger
                            }

                            Label {
                                text: root.controller.status === "ok" ? "Ready" : root.controller.status
                                color: root.controller.status === "ok" ? AppTheme.result : AppTheme.danger
                                font.family: AppTheme.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
