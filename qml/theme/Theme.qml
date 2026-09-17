pragma Singleton
import QtQuick

QtObject {
    property bool dark: false
    readonly property color background: dark ? "#111318" : "#F7F8FA"
    readonly property color surface: dark ? "#1B1E25" : "#FFFFFF"
    readonly property color hoveredSurface: dark ? "#292F3C" : "#EEF2FA"
    readonly property color textPrimary: dark ? "#F2F4F7" : "#171A21"
    readonly property color textSecondary: dark ? "#AAB2C0" : "#5C6472"
    readonly property color accent: dark ? "#7EA2FF" : "#245BDC"
    readonly property color border: dark ? "#343A46" : "#DDE2EA"
    readonly property color error: dark ? "#FF8A80" : "#B42318"
}
