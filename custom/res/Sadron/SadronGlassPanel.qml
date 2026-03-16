import QtQuick

import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    default property alias contentData: contentItem.data

    property real padding: ScreenTools.defaultFontPixelWidth
    property real radius: ScreenTools.defaultFontPixelHeight * 0.5
    property real borderWidth: 1
    property real panelOpacity: qgcPal.globalTheme === QGCPalette.Light ? 0.92 : 0.82
    property bool elevated: true
    property color panelColor: qgcPal.globalTheme === QGCPalette.Light ? qgcPal.window : qgcPal.windowShadeDark
    property color borderColor: qgcPal.globalTheme === QGCPalette.Light
                                ? Qt.rgba(0.06, 0.09, 0.13, 0.08)
                                : Qt.rgba(1, 1, 1, 0.08)
    property color shadowColor: qgcPal.globalTheme === QGCPalette.Light
                                ? Qt.rgba(0.06, 0.09, 0.13, 0.10)
                                : Qt.rgba(0, 0, 0, 0.22)

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: true
    }

    implicitWidth: contentItem.childrenRect.width + (padding * 2)
    implicitHeight: contentItem.childrenRect.height + (padding * 2)

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: elevated ? ScreenTools.defaultFontPixelHeight * 0.18 : 0
        radius: root.radius
        color: root.shadowColor
        opacity: elevated ? 1 : 0
    }

    Rectangle {
        id: panelBackground
        anchors.fill: parent
        radius: root.radius
        border.width: root.borderWidth
        border.color: root.borderColor
        color: Qt.rgba(root.panelColor.r, root.panelColor.g, root.panelColor.b, root.panelOpacity)
    }

    Item {
        id: contentItem
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
