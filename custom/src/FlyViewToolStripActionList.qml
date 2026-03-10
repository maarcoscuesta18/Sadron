import QtQml.Models

import QGroundControl
import QGroundControl.Controls

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist

    model: [
        PreFlightCheckListShowAction { onTriggered: displayPreFlightChecklist() },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionRTL { },
        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },
        // SAR: Mark Target action
        GuidedToolStripAction {
            text:       _guidedController._customController.markTargetTitle
            iconSource: "/res/target.svg"
            visible:    true
            enabled:    QGroundControl.multiVehicleManager.activeVehicleAvailable
            actionID:   _guidedController._customController.actionMarkTarget
        },
        // SAR: Investigate nearest target
        GuidedToolStripAction {
            text:       _guidedController._customController.investigateTitle
            iconSource: "/res/search.svg"
            visible:    sarTargetManager ? sarTargetManager.totalTargets > 0 : false
            enabled:    QGroundControl.multiVehicleManager.activeVehicleAvailable
            actionID:   _guidedController._customController.actionInvestigateTarget
        },
        // SAR: Broadcast alert to mesh
        GuidedToolStripAction {
            text:       _guidedController._customController.broadcastAlertTitle
            iconSource: "/res/alert.svg"
            visible:    meshNetworkManager ? meshNetworkManager.nodeCount > 0 : false
            enabled:    true
            actionID:   _guidedController._customController.actionBroadcastAlert
        }
    ]
}
