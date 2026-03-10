# Multi-Drone Flight Trail Visualization for SAR Missions

## Problem
Currently, QGC's built-in trajectory trail (`FlyViewMap.qml:239-259`) only shows the **active vehicle's** flight path as a single red `MapPolyline`. During a multi-drone SAR mission, there's no way to see what areas all drones have collectively traversed.

## Solution
Create a new C++ backend class `SARFlightTrailManager` that tracks position history for all vehicles, paired with a new QML overlay `SARFlightTrailOverlay.qml` that renders a `MapPolyline` per drone with a distinct color.

---

## Files to Create

### 1. `custom/src/SAR/SARFlightTrailManager.h`
- Class `SARFlightTrailManager : public QObject`
- Properties: `enabled`, `tracking`, `vehicleIds`, `maxPointsPerVehicle`, `minDistanceMeters`
- Q_INVOKABLE methods: `trailForVehicle(vehicleId)`, `colorForVehicle(vehicleId)`, `trailPointCount(vehicleId)`, `startTracking()`, `stopTracking()`, `clearAllTrails()`, `clearTrailForVehicle(vehicleId)`
- `processMavlinkMessage()` — processes `GLOBAL_POSITION_INT` messages
- Signals: `enabledChanged`, `trackingChanged`, `trailsChanged`, `trailUpdated(vehicleId)`, `maxPointsChanged`, `minDistanceChanged`
- Internal: `QHash<int, QList<QGeoCoordinate>> _trails`, color palette of ~8 distinct colors, distance-based point throttling

### 2. `custom/src/SAR/SARFlightTrailManager.cc`
- `processMavlinkMessage()`: decode GLOBAL_POSITION_INT, throttle by min distance, append to trail, trim to maxPoints, emit trailUpdated
- `startTracking()` / `stopTracking()` / `clearAllTrails()` lifecycle
- Color palette: cyan, magenta, yellow, lime, orange, pink, light blue, red — cycling assignment
- `trailForVehicle()` returns `QVariantList` of `QGeoCoordinate` for QML `MapPolyline.path`

### 3. `custom/res/Sadron/SARFlightTrailOverlay.qml`
- Repeater over `sarFlightTrailManager.vehicleIds`
- Each delegate: `MapPolyline` with `line.width: 3`, `line.color: sarFlightTrailManager.colorForVehicle(vehicleId)`, `path: sarFlightTrailManager.trailForVehicle(vehicleId)`
- Connections to `sarFlightTrailManager.trailUpdated` to refresh path
- Semi-transparent, z-order below vehicles but above map tiles

---

## Files to Modify

### 4. `custom/src/SadronPlugin.h`
- Add forward declaration: `class SARFlightTrailManager;`
- Add member: `SARFlightTrailManager *_sarFlightTrailManager = nullptr;`
- Add accessor: `SARFlightTrailManager *sarFlightTrailManager() const`

### 5. `custom/src/SadronPlugin.cc`
- In `init()`: instantiate `_sarFlightTrailManager = new SARFlightTrailManager(this);`
- In `createQmlApplicationEngine()`: `context->setContextProperty("sarFlightTrailManager", _sarFlightTrailManager);`
- In `mavlinkMessage()`: `_sarFlightTrailManager->processMavlinkMessage(vehicle, message);`
- Add `#include "SARFlightTrailManager.h"`

### 6. `custom/src/FlyViewCustomLayer.qml`
- Add a new `Loader` block for `SARFlightTrailOverlay.qml` (after transect visuals, before coverage overlay)
- Active when `sarFlightTrailManager && sarFlightTrailManager.enabled`
- Pass `mapControl` binding

### 7. `custom/custom.qrc`
- Add `<file alias="SARFlightTrailOverlay.qml">res/Sadron/SARFlightTrailOverlay.qml</file>` to the Sadron prefix

### 8. `custom/CMakeLists.txt`
- Add `SARFlightTrailManager.cc` and `SARFlightTrailManager.h` to `CUSTOM_SOURCES`

### 9. `custom/src/SAR/SARMissionManager.cc` + `.h`
- Add `SARFlightTrailManager *` member and constructor parameter
- In `startOperation()`: call `_flightTrailManager->startTracking()`
- In `abortOperation()`: call `_flightTrailManager->stopTracking()`
- OR: Keep lifecycle in SadronPlugin.cc by connecting to `SARMissionManager::operationStarted` signal

---

## Design Decisions
- **Separate from coverage tracker**: Coverage uses grid cells for "what's been scanned by camera FOV". Trails show raw flight paths.
- **Color per vehicle**: 8-color palette, cycling. Each drone gets a distinct persistent color.
- **Performance**: 2m min distance throttling + 5000 max points per vehicle cap.
- **Trail persistence**: Trails persist after mission stop for debriefing review. Cleared on next `startTracking()` or explicit `clearAllTrails()`.
- **Signal-based approach preferred**: Connect `operationStarted` signal to `startTracking()` rather than adding direct dependency in SARMissionManager constructor. Keeps coupling minimal.
