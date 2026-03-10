import QtQuick
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Places SARTargetMarker items on the flight map as MapQuickItems
Item {
    id: _root

    property var mapControl

    Repeater {
        model: sarTargetManager ? sarTargetManager.targets : null

        MapQuickItem {
            coordinate:     object.coordinate
            anchorPoint.x:  sourceItem.width / 2
            anchorPoint.y:  sourceItem.height / 2
            z:              QGroundControl.zOrderMapItems + 1

            sourceItem: Loader {
                property var target: object
                source: "qrc:/custom/Sadron/SARTargetMarker.qml"
                onLoaded: item.target = Qt.binding(function() { return target })
            }

            parent: mapControl
        }
    }
}
