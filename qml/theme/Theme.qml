pragma Singleton
import QtQuick

QtObject {
    property bool dark: false
    property int openMenuCount: 0
    readonly property color background: dark ? "#141917" : "#F6F7F3"
    readonly property color surface: dark ? "#1D2420" : "#FFFFFF"
    readonly property color hoveredSurface: dark ? "#29372F" : "#EAF0E8"
    readonly property color textPrimary: dark ? "#EEF2EB" : "#202C25"
    readonly property color textSecondary: dark ? "#ADB9AF" : "#637267"
    readonly property color accent: dark ? "#8DD5AE" : "#256747"
    readonly property color accentText: dark ? "#122C20" : "#FFFFFF"
    readonly property color border: dark ? "#354239" : "#DCE3D9"
    readonly property int radius: 12
    readonly property int pageTitleSize: 30
    readonly property int sectionTitleSize: 20
    readonly property color error: dark ? "#FF8A80" : "#B42318"
}
