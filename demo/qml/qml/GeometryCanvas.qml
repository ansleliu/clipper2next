import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Clipper2NextDemo 1.0

Item {
    id: root

    required property var controller
    property bool showGrid: true
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0

    Rectangle {
        anchors.fill: parent
        radius: AppTheme.radius
        color: AppTheme.canvas
        border.color: AppTheme.line
        border.width: 1
        clip: true
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 1
        renderTarget: Canvas.Image

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const started = Date.now()
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            try {
                drawBackground(ctx)
                drawScene(ctx)
            } catch (error) {
                drawBackground(ctx)
                ctx.save()
                ctx.fillStyle = "#bf3d2e"
                ctx.font = "13px " + AppTheme.fontFamily
                ctx.fillText("Canvas draw failed: " + error, 24, 58)
                ctx.restore()
                console.warn("Canvas draw failed:", error)
            }
            root.controller.setRenderMilliseconds(Date.now() - started)
        }

        function emptyBounds() {
            return { left: 0, top: 0, right: 1, bottom: 1, hasData: false }
        }

        function includePoint(bounds, point) {
            if (!bounds.hasData) {
                bounds.left = point.x
                bounds.right = point.x
                bounds.top = point.y
                bounds.bottom = point.y
                bounds.hasData = true
                return
            }
            bounds.left = Math.min(bounds.left, point.x)
            bounds.right = Math.max(bounds.right, point.x)
            bounds.top = Math.min(bounds.top, point.y)
            bounds.bottom = Math.max(bounds.bottom, point.y)
        }

        function includePaths(bounds, paths) {
            for (let pathIndex = 0; pathIndex < paths.length; ++pathIndex) {
                const path = paths[pathIndex]
                for (let pointIndex = 0; pointIndex < path.length; ++pointIndex)
                    includePoint(bounds, path[pointIndex])
            }
        }

        function includeRect(bounds, rect) {
            if (rect.right <= rect.left || rect.bottom <= rect.top)
                return
            includePoint(bounds, { x: rect.left, y: rect.top })
            includePoint(bounds, { x: rect.right, y: rect.bottom })
        }

        function sceneBounds() {
            const bounds = emptyBounds()
            includePaths(bounds, root.controller.subjects)
            if (root.controller.scene === "triangulation") {
                if (!bounds.hasData)
                    return { left: 0, top: 0, right: 100, bottom: 100, hasData: true }
                return bounds
            }
            includePaths(bounds, root.controller.clips)
            includePaths(bounds, root.controller.results)
            includeRect(bounds, root.controller.clipRect)
            if (!bounds.hasData)
                return { left: 0, top: 0, right: 100, bottom: 100, hasData: true }
            return bounds
        }

        function transformFor(bounds) {
            const margin = 42
            const spanX = Math.max(1, bounds.right - bounds.left)
            const spanY = Math.max(1, bounds.bottom - bounds.top)
            const scale = Math.min((width - margin * 2) / spanX,
                                   (height - margin * 2) / spanY) * root.zoom
            const centerX = (bounds.left + bounds.right) / 2
            const centerY = (bounds.top + bounds.bottom) / 2
            return {
                scale: scale,
                mapX: function(x) { return width / 2 + root.panX + (x - centerX) * scale },
                mapY: function(y) { return height / 2 + root.panY + (y - centerY) * scale }
            }
        }

        function drawBackground(ctx) {
            ctx.save()
            ctx.fillStyle = "#f7faf9"
            ctx.fillRect(0, 0, width, height)

            const glow = ctx.createRadialGradient(width * 0.22, height * 0.16, 0,
                                                  width * 0.22, height * 0.16,
                                                  Math.max(width, height) * 0.72)
            glow.addColorStop(0, "rgba(15, 118, 110, 0.10)")
            glow.addColorStop(0.42, "rgba(46, 107, 217, 0.045)")
            glow.addColorStop(1, "rgba(255, 255, 255, 0)")
            ctx.fillStyle = glow
            ctx.fillRect(0, 0, width, height)
            ctx.restore()
        }

        function drawGrid(ctx) {
            if (!root.showGrid)
                return
            ctx.save()
            ctx.strokeStyle = "#e8efec"
            ctx.lineWidth = 1
            const step = 40
            for (let x = 0; x < width; x += step) {
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x, height)
                ctx.stroke()
            }
            for (let y = 0; y < height; y += step) {
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }
            ctx.strokeStyle = "#d8e5df"
            const major = step * 4
            for (let mx = 0; mx < width; mx += major) {
                ctx.beginPath()
                ctx.moveTo(mx, 0)
                ctx.lineTo(mx, height)
                ctx.stroke()
            }
            for (let my = 0; my < height; my += major) {
                ctx.beginPath()
                ctx.moveTo(0, my)
                ctx.lineTo(width, my)
                ctx.stroke()
            }
            ctx.restore()
        }

        function drawPaths(ctx, paths, transform, stroke, fill, alpha, widthScale, shadow) {
            ctx.save()
            ctx.globalAlpha = alpha
            ctx.strokeStyle = stroke
            ctx.fillStyle = fill
            ctx.lineWidth = widthScale
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            if (shadow) {
                ctx.shadowColor = "rgba(24, 44, 38, 0.16)"
                ctx.shadowBlur = 12
                ctx.shadowOffsetY = 3
            }
            for (let pathIndex = 0; pathIndex < paths.length; ++pathIndex) {
                const path = paths[pathIndex]
                if (path.length === 0)
                    continue
                ctx.beginPath()
                ctx.moveTo(transform.mapX(path[0].x), transform.mapY(path[0].y))
                for (let pointIndex = 1; pointIndex < path.length; ++pointIndex)
                    ctx.lineTo(transform.mapX(path[pointIndex].x),
                               transform.mapY(path[pointIndex].y))
                ctx.closePath()
                ctx.fill()
                ctx.stroke()
            }
            ctx.restore()
        }

        function drawVertices(ctx, paths, transform, color, maxPoints) {
            let drawn = 0
            ctx.save()
            ctx.fillStyle = color
            ctx.strokeStyle = "rgba(255, 255, 255, 0.84)"
            ctx.lineWidth = 1
            for (let pathIndex = 0; pathIndex < paths.length; ++pathIndex) {
                const path = paths[pathIndex]
                for (let pointIndex = 0; pointIndex < path.length; ++pointIndex) {
                    if (drawn >= maxPoints) {
                        ctx.restore()
                        return
                    }
                    const x = transform.mapX(path[pointIndex].x)
                    const y = transform.mapY(path[pointIndex].y)
                    ctx.beginPath()
                    ctx.arc(x, y, 2.6, 0, Math.PI * 2)
                    ctx.fill()
                    ctx.stroke()
                    ++drawn
                }
            }
            ctx.restore()
        }

        function drawRect(ctx, rect, transform) {
            if (rect.right <= rect.left || rect.bottom <= rect.top)
                return
            ctx.save()
            const left = transform.mapX(rect.left)
            const top = transform.mapY(rect.top)
            const right = transform.mapX(rect.right)
            const bottom = transform.mapY(rect.bottom)
            ctx.fillStyle = "rgba(214, 106, 60, 0.055)"
            ctx.fillRect(left, top, right - left, bottom - top)
            ctx.strokeStyle = "#d66a3c"
            ctx.lineWidth = 1.8
            ctx.setLineDash([8, 6])
            ctx.strokeRect(left, top, right - left, bottom - top)
            ctx.restore()
        }

        function drawScene(ctx) {
            drawGrid(ctx)
            const bounds = sceneBounds()
            const transform = transformFor(bounds)
            if (root.controller.scene === "triangulation") {
                drawPaths(ctx, root.controller.subjects, transform, "#2e6bd9", "rgba(46, 107, 217, 0.10)", 1.0, 1.8, false)
                drawPaths(ctx, root.controller.results, transform, "#16845f", "rgba(22, 132, 95, 0.11)", 0.9, 1.1, false)
                return
            }
            drawPaths(ctx, root.controller.subjects, transform, "#2e6bd9", "rgba(46, 107, 217, 0.12)", 1.0, 1.35, false)
            drawPaths(ctx, root.controller.clips, transform, "#d66a3c", "rgba(214, 106, 60, 0.12)", 1.0, 1.35, false)
            drawPaths(ctx, root.controller.results, transform, "#16845f", "rgba(22, 132, 95, 0.30)", 1.0, 2.25, true)
            drawRect(ctx, root.controller.clipRect, transform)
            drawVertices(ctx, root.controller.results, transform, "#16845f", 360)
        }
    }

    Timer {
        interval: 80
        running: true
        repeat: false
        onTriggered: canvas.requestPaint()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real previousX: 0
        property real previousY: 0

        onPressed: function(mouse) {
            previousX = mouse.x
            previousY = mouse.y
        }

        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            root.panX += mouse.x - previousX
            root.panY += mouse.y - previousY
            previousX = mouse.x
            previousY = mouse.y
            canvas.requestPaint()
        }

        onWheel: function(wheel) {
            root.zoom = Math.max(0.2, Math.min(8.0, root.zoom * (wheel.angleDelta.y > 0 ? 1.12 : 0.88)))
            canvas.requestPaint()
        }
    }

    Connections {
        target: root.controller
        function onGeometryChanged() { canvas.requestPaint() }
        function onViewResetRequested() {
            root.zoom = 1.0
            root.panX = 0
            root.panY = 0
            canvas.requestPaint()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        width: legend.implicitWidth + 22
        height: legend.implicitHeight + 18
        radius: AppTheme.radius
        color: Qt.rgba(1, 1, 1, 0.86)
        border.color: AppTheme.lineSoft

        RowLayout {
            id: legend
            anchors.centerIn: parent
            spacing: 12

            Repeater {
                model: [
                    { "label": "Subject", "color": AppTheme.subject },
                    { "label": "Clip", "color": AppTheme.clip },
                    { "label": "Result", "color": AppTheme.result }
                ]

                RowLayout {
                    spacing: 6

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        color: modelData.color
                    }

                    Label {
                        text: modelData.label
                        color: AppTheme.muted
                        font.family: AppTheme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        width: viewControls.implicitWidth + 12
        height: viewControls.implicitHeight + 12
        radius: AppTheme.radius
        color: Qt.rgba(1, 1, 1, 0.88)
        border.color: AppTheme.lineSoft

        RowLayout {
            id: viewControls
            anchors.centerIn: parent
            spacing: 5

            DesignButton {
                text: "-"
                variant: "ghost"
                implicitWidth: 32
                onClicked: {
                    root.zoom = Math.max(0.2, root.zoom * 0.85)
                    canvas.requestPaint()
                }
            }

            DesignButton {
                text: "+"
                variant: "ghost"
                implicitWidth: 32
                onClicked: {
                    root.zoom = Math.min(8.0, root.zoom * 1.15)
                    canvas.requestPaint()
                }
            }

            DesignButton {
                text: "Fit"
                variant: "ghost"
                onClicked: root.controller.resetView()
            }

            DesignButton {
                text: "Grid"
                variant: "ghost"
                checkable: true
                checked: root.showGrid
                onClicked: {
                    root.showGrid = !root.showGrid
                    canvas.requestPaint()
                }
            }
        }
    }
}
