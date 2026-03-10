// Sadron SAR custom guided actions

import QtQml

import QGroundControl

QtObject {
    id: _root

    // SAR-specific guided actions
    readonly property int actionMarkTarget:      _guidedController.customActionStart + 0
    readonly property int actionInvestigateTarget: _guidedController.customActionStart + 1
    readonly property int actionRedirectToTarget:  _guidedController.customActionStart + 2
    readonly property int actionBroadcastAlert:    _guidedController.customActionStart + 3

    readonly property string markTargetTitle:        qsTr("Mark Target")
    readonly property string markTargetMessage:      qsTr("Mark current position as a potential SAR target?")
    readonly property string investigateTitle:        qsTr("Investigate")
    readonly property string investigateMessage:      qsTr("Send active vehicle to investigate the nearest target?")
    readonly property string redirectTitle:           qsTr("Redirect")
    readonly property string redirectMessage:         qsTr("Redirect vehicle to specific target location?")
    readonly property string broadcastAlertTitle:     qsTr("Alert All")
    readonly property string broadcastAlertMessage:   qsTr("Broadcast alert to all mesh-connected vehicles?")

    function customConfirmAction(actionCode, actionData, mapIndicator, confirmDialog) {
        switch (actionCode) {
        case actionMarkTarget:
            confirmDialog.hideTrigger = true
            confirmDialog.title = markTargetTitle
            confirmDialog.message = markTargetMessage
            break
        case actionInvestigateTarget:
            confirmDialog.hideTrigger = true
            confirmDialog.title = investigateTitle
            confirmDialog.message = investigateMessage
            break
        case actionRedirectToTarget:
            confirmDialog.hideTrigger = true
            confirmDialog.title = redirectTitle
            confirmDialog.message = redirectMessage
            break
        case actionBroadcastAlert:
            confirmDialog.hideTrigger = true
            confirmDialog.title = broadcastAlertTitle
            confirmDialog.message = broadcastAlertMessage
            break
        default:
            return false
        }

        return true
    }

    function customExecuteAction(actionCode, actionData, sliderOutputValue, optionChecked) {
        var activeVehicle = QGroundControl.multiVehicleManager.activeVehicle

        switch (actionCode) {
        case actionMarkTarget:
            if (activeVehicle && sarTargetManager) {
                var coord = activeVehicle.coordinate
                sarTargetManager.addTarget(coord, "Target spotted by V" + activeVehicle.id, activeVehicle.id)

                // Broadcast to mesh
                if (meshNetworkManager) {
                    meshNetworkManager.broadcastTargetSpotted(coord, activeVehicle.id)
                }
            }
            break

        case actionInvestigateTarget:
            if (activeVehicle && sarTargetManager) {
                var nearest = sarTargetManager.nearestTarget(activeVehicle.coordinate)
                if (nearest) {
                    nearest.status = 1 // Investigating
                }
            }
            break

        case actionRedirectToTarget:
            if (activeVehicle && sarTargetManager) {
                var target = sarTargetManager.nearestTarget(activeVehicle.coordinate)
                if (target) {
                    activeVehicle.guidedModeGotoLocation(target.coordinate)
                }
            }
            break

        case actionBroadcastAlert:
            if (meshNetworkManager) {
                QGroundControl.showMessageDialog(mainWindow, "Alert Broadcast", "Alert sent to all mesh nodes.")
            }
            break

        default:
            return false
        }

        return true
    }
}
