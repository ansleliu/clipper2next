pragma Singleton

import QtQuick

QtObject {
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI Variable" : "Inter"

    readonly property color window: "#eef2f5"
    readonly property color surface: "#ffffff"
    readonly property color surfaceRaised: "#f9fbfc"
    readonly property color canvas: "#f7faf9"
    readonly property color ink: "#171c22"
    readonly property color text: "#27313c"
    readonly property color muted: "#66727f"
    readonly property color faint: "#8b97a3"
    readonly property color line: "#d9e1e8"
    readonly property color lineSoft: "#e9eef2"

    readonly property color accent: "#0f766e"
    readonly property color accentSoft: "#dff5f1"
    readonly property color subject: "#2e6bd9"
    readonly property color subjectSoft: "#dce9ff"
    readonly property color clip: "#d66a3c"
    readonly property color clipSoft: "#ffe6db"
    readonly property color result: "#16845f"
    readonly property color resultSoft: "#dcf4e9"
    readonly property color warning: "#b57916"
    readonly property color danger: "#bf3d2e"

    readonly property int radius: 8
    readonly property int controlRadius: 7
    readonly property int panelPadding: 16
}
