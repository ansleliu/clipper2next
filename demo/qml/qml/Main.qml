import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

ApplicationWindow {
    id: window

    width: 1280
    height: 820
    minimumWidth: 1060
    minimumHeight: 700
    visible: true
    title: "clipper2next demo"
    color: AppTheme.window
    font.family: AppTheme.fontFamily

    DemoController {
        id: demo
    }

    Component.onCompleted: {
        const hasInitialState = initialDemoScene.length > 0
                || initialDemoOperation.length > 0
                || initialDemoRectMode.length > 0
        if (initialDemoScene.length > 0)
            demo.scene = initialDemoScene
        if (initialDemoOperation.length > 0)
            demo.operation = initialDemoOperation
        if (initialDemoRectMode.length > 0)
            demo.rectClipMode = initialDemoRectMode
        if (hasInitialState)
            demo.run()
    }

    Rectangle {
        anchors.fill: parent
        color: AppTheme.window

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            CommandBar {
                controller: demo
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 12
                spacing: 14

                GeometryCanvas {
                    id: geometryCanvas
                    controller: demo
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                InspectorPanel {
                    controller: demo
                    Layout.preferredWidth: 382
                    Layout.minimumWidth: 352
                    Layout.maximumWidth: 420
                    Layout.fillHeight: true
                }
            }

            MetricsBar {
                controller: demo
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 14
            }
        }
    }
}
