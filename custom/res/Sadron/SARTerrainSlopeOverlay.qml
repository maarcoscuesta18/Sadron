import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.Controls

// Map overlay: terrain slope analysis — colored grid cells from Copernicus elevation data
// Green (flat) → Yellow (moderate) → Orange (steep) → Red (very steep)
Item {
    id: slopeOverlay

    property var mapControl

    property real _margin: ScreenTools.defaultFontPixelWidth * 0.75

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    // ── Slope color helper ──
    function slopeColor(slopeDeg) {
        if (slopeDeg < 5)   return Qt.rgba(0.30, 0.69, 0.31, 0.35)    // Green — flat
        if (slopeDeg < 15)  return Qt.rgba(0.55, 0.76, 0.29, 0.40)    // Light green
        if (slopeDeg < 25)  return Qt.rgba(1.00, 0.92, 0.23, 0.45)    // Yellow — moderate
        if (slopeDeg < 35)  return Qt.rgba(1.00, 0.60, 0.00, 0.50)    // Orange — steep
        if (slopeDeg < 45)  return Qt.rgba(0.96, 0.26, 0.21, 0.55)    // Red — very steep
        return Qt.rgba(0.62, 0.16, 0.16, 0.60)                         // Dark red — cliff
    }

    function slopeLabel(slopeDeg) {
        if (slopeDeg < 5)   return "Flat"
        if (slopeDeg < 15)  return "Gentle"
        if (slopeDeg < 25)  return "Moderate"
        if (slopeDeg < 35)  return "Steep"
        if (slopeDeg < 45)  return "Very Steep"
        return "Cliff"
    }

    // 3C: Compute cell pixel size once at overlay level (shared across all cells)
    property real _cellPixelSize: {
        if (!mapControl || !environmentalDataProvider) return 8
        var _z = mapControl.zoomLevel  // force re-eval on zoom
        var rows = environmentalDataProvider.slopeRows
        var cols = environmentalDataProvider.slopeCols
        if (rows < 2 || cols < 2) return 8
        var grid = environmentalDataProvider.slopeGrid
        if (grid.length < 2) return 8
        var p0 = mapControl.fromCoordinate(
            QtPositioning.coordinate(grid[0].latitude, grid[0].longitude), false)
        var p1 = mapControl.fromCoordinate(
            QtPositioning.coordinate(grid[1].latitude, grid[1].longitude), false)
        var dx = Math.abs(p1.x - p0.x)
        return Math.max(4, dx)
    }

    // 3C: Use MapQuickItem for slope cells — lets the map framework handle
    // coordinate→screen positioning natively, avoiding per-cell fromCoordinate() calls.
    // Removed per-cell MouseArea + ToolTip (hoverEnabled on hundreds of items is expensive).
    Repeater {
        id: slopeGridRepeater
        model: environmentalDataProvider ? environmentalDataProvider.slopeGrid : []

        MapQuickItem {
            parent:     mapControl
            coordinate: QtPositioning.coordinate(
                            modelData ? modelData.latitude  : 0,
                            modelData ? modelData.longitude : 0)
            anchorPoint.x: _cellPixelSize / 2
            anchorPoint.y: _cellPixelSize / 2

            property real slope:  modelData ? modelData.slope  : 0
            property real aspect: modelData ? modelData.aspect : 0

            sourceItem: Rectangle {
                width:   _cellPixelSize
                height:  _cellPixelSize
                color:   slopeColor(slope)
                radius:  1

                // Aspect indicator — tiny line showing direction of steepest descent
                Rectangle {
                    visible:            slope > 5 && parent.width > 10
                    anchors.centerIn:   parent
                    width:              1
                    height:             parent.height * 0.4
                    color:              Qt.rgba(1, 1, 1, 0.4)
                    rotation:           aspect
                    transformOrigin:    Item.Center
                }
            }
        }
    }

    // ── Slope Summary + Legend Panel ──
    Rectangle {
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.margins:    _margin * 2
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 34
        width:              slopeLegendCol.width + _margin * 3
        height:             slopeLegendCol.height + _margin * 2
        radius:             ScreenTools.defaultFontPixelHeight * 0.3
        color:              Qt.rgba(0, 0, 0, 0.75)
        border.color:       Qt.rgba(1, 1, 1, 0.15)
        border.width:       1
        visible:            environmentalDataProvider && environmentalDataProvider.slopeDataAvailable

        ColumnLayout {
            id:                 slopeLegendCol
            anchors.centerIn:   parent
            spacing:            3

            QGCLabel {
                text:       "TERRAIN SLOPE"
                color:      "#ff9800"
                font.bold:  true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            // Summary stats
            RowLayout {
                spacing: _margin * 2

                QGCLabel {
                    text:   "Min: " + (environmentalDataProvider ? environmentalDataProvider.minSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#4caf50"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }

                QGCLabel {
                    text:   "Avg: " + (environmentalDataProvider ? environmentalDataProvider.avgSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#ffeb3b"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }

                QGCLabel {
                    text:   "Max: " + (environmentalDataProvider ? environmentalDataProvider.maxSlope.toFixed(1) : "0") + "\u00B0"
                    color:  "#f44336"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.9
                }
            }

            // Color scale legend
            Repeater {
                model: [
                    { label: "0-5\u00B0   Flat",         clr: "#4caf50" },
                    { label: "5-15\u00B0  Gentle",       clr: "#8bc34a" },
                    { label: "15-25\u00B0 Moderate",     clr: "#ffeb3b" },
                    { label: "25-35\u00B0 Steep",        clr: "#ff9800" },
                    { label: "35-45\u00B0 Very Steep",   clr: "#f44336" },
                    { label: "> 45\u00B0  Cliff",        clr: "#9e2929" }
                ]

                RowLayout {
                    spacing: _margin * 0.5

                    Rectangle {
                        width:  ScreenTools.defaultFontPixelHeight * 0.6
                        height: width
                        radius: 2
                        color:  modelData.clr
                    }

                    QGCLabel {
                        text:   modelData.label
                        color:  "#ccc"
                        font.pointSize: ScreenTools.smallFontPointSize * 0.8
                    }
                }
            }
        }
    }

    // ── Loading indicator ──
    Rectangle {
        anchors.centerIn:   parent
        width:              loadingLabel.width + _margin * 4
        height:             loadingLabel.height + _margin * 2
        radius:             4
        color:              Qt.rgba(0, 0, 0, 0.8)
        visible:            environmentalDataProvider && environmentalDataProvider.slopeLoading

        QGCLabel {
            id:                 loadingLabel
            anchors.centerIn:   parent
            text:               "Computing terrain slope..."
            color:              "#ff9800"
            font.pointSize:     ScreenTools.defaultFontPointSize
        }
    }
}
