# ============================================================================
# Sadron SAR Plugin for QGroundControl
# Search and Rescue system for mesh drone operations
# ============================================================================

message(STATUS "Sadron: Adding SAR Custom Plugin")

# ============================================================================
# Custom Android Package Template
# ============================================================================

if(ANDROID)
    set(CUSTOM_ANDROID_DIR "${CMAKE_SOURCE_DIR}/custom/android")
    if(EXISTS "${CUSTOM_ANDROID_DIR}")
        file(GLOB CUSTOM_ANDROID_FILES "${CUSTOM_ANDROID_DIR}/*")
        if(CUSTOM_ANDROID_FILES)
            message(STATUS "Sadron: Custom Android package template found. Overlaying custom files...")
            set(DEFAULT_ANDROID_DIR "${CMAKE_SOURCE_DIR}/android")
            set(FINAL_ANDROID_DIR "${CMAKE_BINARY_DIR}/custom/android")

            file(COPY "${DEFAULT_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            file(COPY "${CUSTOM_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")

            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${DEFAULT_ANDROID_DIR}/"
                "${CUSTOM_ANDROID_DIR}/"
            )

            set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${FINAL_ANDROID_DIR}" CACHE PATH "Path to a custom Android package template" FORCE)
            message(STATUS "Sadron: Android package template path will be set to: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
        else()
            message(STATUS "Sadron: Custom Android package template empty. Using default.")
        endif()
    else()
        message(STATUS "Sadron: No custom Android package template found. Using default.")
    endif()
endif()

# ============================================================================
# Custom Resources and Import Paths
# ============================================================================

list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

set(QML_IMPORT_PATH ${QML_IMPORT_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/res" CACHE STRING "Extra qml import paths" FORCE)

# ============================================================================
# Custom QML Module — Widgets (inherited from custom-example)
# ============================================================================

qt_add_library(CustomModule STATIC)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomArtificialHorizon.qml PROPERTIES QT_RESOURCE_ALIAS CustomArtificialHorizon.qml)
qgc_set_qt_resource_alias(
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomAttitudeWidget.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomIconButton.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomOnOffSwitch.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomQuickButton.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomSignalStrength.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomToolBarButton.qml
    ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomVehicleButton.qml
)

qt_add_qml_module(CustomModule
    URI Custom.Widgets
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomArtificialHorizon.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomAttitudeWidget.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomIconButton.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomOnOffSwitch.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomQuickButton.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomSignalStrength.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomToolBarButton.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/res/Custom/Widgets/CustomVehicleButton.qml
    NO_PLUGIN
)

# ============================================================================
# Custom Plugin Sources
# ============================================================================

set(CUSTOM_SOURCES
    # Core plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SadronPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SadronPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin/CustomAutoPilotPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin/CustomAutoPilotPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePluginFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Video/SadronVideoConfig.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Video/SadronVideoConfig.h
    # SAR subsystem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARMissionManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARMissionManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARZoneManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARZoneManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARTargetManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARTargetManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARCoverageTracker.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARCoverageTracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARGeoUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARReTaskingManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARReTaskingManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/VehicleCoordinator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/VehicleCoordinator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARModeManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR/SARModeManager.h
    # Mesh networking
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Mesh/MeshNetworkManager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Mesh/MeshNetworkManager.h
    # Environmental data overlays
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Environmental/EnvironmentalDataProvider.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Environmental/EnvironmentalDataProvider.h
    CACHE INTERNAL "" FORCE
)

# ============================================================================
# Custom Build Configuration
# ============================================================================

set(CUSTOM_LIBRARIES
    CustomModule
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Video
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SAR
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Environmental
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="SadronPlugin.h"
    CUSTOMCLASS=SadronPlugin
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_QT_COMPONENTS
    Core
    Network
    Positioning
    CACHE INTERNAL "" FORCE
)
