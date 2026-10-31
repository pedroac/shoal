# core: the model, files, validation and export. QtCore only, so it is tested headless.
add_library(shoal_core STATIC
    core/AssetCommands.cpp
    core/AssetCommands.h
    core/ClassCommands.cpp
    core/ClassCommands.h
    core/CommandStack.cpp
    core/CommandStack.h
    core/FlowCommands.cpp
    core/FlowCommands.h
    core/JsonReader.cpp
    core/JsonReader.h
    core/JsonWriter.cpp
    core/JsonWriter.h
    core/Model.cpp
    core/Model.h
    core/ProjectCodec.cpp
    core/ProjectCodec.h
    core/ProjectDocument.cpp
    core/ProjectDocument.h
    core/ProjectStore.cpp
    core/ProjectStore.h
    core/PresetCommands.cpp
    core/PresetCommands.h
    core/Presets.cpp
    core/Presets.h
    core/ProjectTemplates.cpp
    core/ProjectTemplates.h
    core/SceneCommands.cpp
    core/SceneCommands.h
    core/SceneTree.cpp
    core/SceneTree.h
    core/TransitionTypes.cpp
    core/TransitionTypes.h
    core/Version.cpp
    core/Version.h
)
add_library(shoal::core ALIAS shoal_core)
target_include_directories(shoal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_core PUBLIC Qt6::Core)
target_compile_definitions(shoal_core PRIVATE SHOAL_VERSION_STRING="${SHOAL_VERSION_TAG}")
shoal_set_warnings(shoal_core)

# The built-in transition types: every file in assets/transitions, and their previews.
file(GLOB shoal_transition_types CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/transitions/*.json"
    "${PROJECT_SOURCE_DIR}/assets/transitions/*.gif")
qt_add_resources(shoal_core "transition_types"
    PREFIX "/shoal/transitions"
    BASE "${PROJECT_SOURCE_DIR}/assets/transitions"
    FILES ${shoal_transition_types})

# assets: sprat sheets and their atlases, through sprat-lib.
add_library(shoal_assets STATIC
    assets/FontCache.cpp
    assets/FontCache.h
    assets/SheetCache.cpp
    assets/SheetCache.h
    assets/SpratLibrary.cpp
    assets/SpratLibrary.h
    assets/SpriteSheet.cpp
    assets/SpriteSheet.h
)
add_library(shoal::assets ALIAS shoal_assets)
target_include_directories(shoal_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_assets PUBLIC shoal::core Qt6::Gui PRIVATE sprat::sprat)
shoal_set_warnings(shoal_assets)

# editor: the Qt Widgets application, as a library so tests can create its windows.
add_library(shoal_editor STATIC
    editor/AssetBrowserPanel.cpp
    editor/AssetBrowserPanel.h
    editor/AssetField.cpp
    editor/AssetField.h
    editor/AssetItem.cpp
    editor/AssetItem.h
    editor/CanvasView.cpp
    editor/CanvasView.h
    editor/ClickOrDragView.h
    editor/ColorButton.cpp
    editor/ColorButton.h
    editor/EntityItem.cpp
    editor/EntityItem.h
    editor/FlowScene.cpp
    editor/FlowScene.h
    editor/FlowView.cpp
    editor/FlowView.h
    editor/FormFields.cpp
    editor/FormFields.h
    editor/Icons.cpp
    editor/Icons.h
    editor/InspectorPanel.cpp
    editor/InspectorPanel.h
    editor/MainWindow.cpp
    editor/MainWindow.h
    editor/NewProjectDialog.cpp
    editor/NewProjectDialog.h
    editor/PointGrid.cpp
    editor/PointGrid.h
    editor/PresetSettingsPanel.cpp
    editor/PresetSettingsPanel.h
    editor/PresetsPanel.cpp
    editor/PresetsPanel.h
    editor/PresetsView.cpp
    editor/PresetsView.h
    editor/ProjectDropArea.cpp
    editor/ProjectDropArea.h
    editor/SceneCanvas.cpp
    editor/SceneCanvas.h
    editor/SceneTreePanel.cpp
    editor/SceneTreePanel.h
    editor/SceneView.cpp
    editor/SceneView.h
    editor/SceneNodeItem.cpp
    editor/SceneNodeItem.h
    editor/SceneSettingsPanel.cpp
    editor/SceneSettingsPanel.h
    editor/ScenesPanel.cpp
    editor/ScenesPanel.h
    editor/Splitter.cpp
    editor/Splitter.h
    editor/Thumbnails.cpp
    editor/Thumbnails.h
    editor/TextLayout.cpp
    editor/TextLayout.h
    editor/TransitionPanel.cpp
    editor/TransitionPanel.h
    editor/TileGridOverlay.cpp
    editor/TileGridOverlay.h
    editor/TransformHandle.cpp
    editor/TransformHandle.h
    editor/TransitionEdgeItem.cpp
    editor/TransitionEdgeItem.h
    editor/WorkspaceTabs.cpp
    editor/WorkspaceTabs.h
)
add_library(shoal::editor ALIAS shoal_editor)
target_include_directories(shoal_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_editor PUBLIC shoal::core shoal::assets Qt6::Widgets PRIVATE Qt6::Svg)

# The editor's icons, from assets/icons, by the names Icons.cpp uses.
set(shoal_icons
    "add-ellipse-svgrepo-com.svg=add.svg"
    "remove-svgrepo-com.svg=remove.svg"
    "filter-svgrepo-com(1).svg=filter.svg"
    "flow-tree-svgrepo-com.svg=flow.svg"
    "cinema-clapper-director-scene-movie-svgrepo-com.svg=scene.svg"
    "template-20px-svgrepo-com.svg=preset.svg"
    "images-967-svgrepo-com.svg=assets.svg"
    "gui-arrow-up-svgrepo-com.svg=move-up.svg"
    "gui-arrow-down-svgrepo-com.svg=move-down.svg"
    "drag-handle-dots-1-svgrepo-com.svg=drag-handle.svg"
    "drag-svgrepo-com.svg=drop.svg"
    "alert-danger-error-warning-svgrepo-com.svg=error.svg"
    "warning-svgrepo-com.svg=warning.svg"
)
set(shoal_icon_files)
foreach(icon IN LISTS shoal_icons)
    string(REPLACE "=" ";" icon "${icon}")
    list(GET icon 0 file)
    list(GET icon 1 alias)
    set(file "${PROJECT_SOURCE_DIR}/assets/icons/${file}")
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
    list(APPEND shoal_icon_files "${file}")
endforeach()
qt_add_resources(shoal_editor "icons" PREFIX "/icons" FILES ${shoal_icon_files})
shoal_set_warnings(shoal_editor)

qt_add_executable(shoal editor/main.cpp)
target_link_libraries(shoal PRIVATE shoal::editor)
set_target_properties(shoal PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME Shoal
    MACOSX_BUNDLE_BUNDLE_VERSION ${SHOAL_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${SHOAL_VERSION}
)
shoal_set_warnings(shoal)
