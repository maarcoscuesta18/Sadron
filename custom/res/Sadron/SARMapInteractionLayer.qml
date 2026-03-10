import QtQuick
import QtQuick.Controls
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Map interaction layer: right-click context menu + click-to-place-target mode
Item {
    id: _root

    property var  mapControl
    property bool editingPolygon:  false
    property bool markTargetMode:  false

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // Mouse overlay — captures right-clicks always, left-clicks only in mark-target mode
    MouseArea {
        id:              interactionArea
        anchors.fill:    parent
        acceptedButtons: Qt.RightButton | (markTargetMode ? Qt.LeftButton : Qt.NoButton)
        enabled:         !editingPolygon

        onClicked: function(mouse) {
            if (!mapControl) return

            var mapPoint = mapControl.mapFromItem(interactionArea, mouse.x, mouse.y)
            var coord = mapControl.toCoordinate(mapPoint, false)
            if (!coord.isValid) return

            if (mouse.button === Qt.RightButton) {
                // Right-click: show context menu
                contextMenu._clickCoord = coord
                contextMenu.popup()
            } else if (mouse.button === Qt.LeftButton && markTargetMode) {
                // Left-click in mark-target mode: place a target
                if (sarTargetManager) {
                    sarTargetManager.addTarget(coord)
                }
            }
        }
    }

    // Right-click context menu
    QGCMenu {
        id: contextMenu

        property var _clickCoord: QtPositioning.coordinate()

        QGCMenuItem {
            text: qsTr("Add Target Here")
            onTriggered: {
                if (sarTargetManager && contextMenu._clickCoord.isValid) {
                    sarTargetManager.addTarget(contextMenu._clickCoord)
                }
            }
        }

        QGCMenuItem {
            text: qsTr("Center Map Here")
            onTriggered: {
                if (mapControl && contextMenu._clickCoord.isValid) {
                    mapControl.center = contextMenu._clickCoord
                }
            }
        }
    }
}
