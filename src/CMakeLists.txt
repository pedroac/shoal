# core: the model, files, validation and export. QtCore only, so it is tested headless.
add_library(shoal_core STATIC
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
    core/Presets.cpp
    core/Presets.h
    core/ProjectTemplates.cpp
    core/ProjectTemplates.h
    core/SceneCommands.cpp
    core/SceneCommands.h
    core/SceneTree.cpp
    core/SceneTree.h
    core/SheetCommands.cpp
    core/SheetCommands.h
    core/Version.cpp
    core/Version.h
)
add_library(shoal::core ALIAS shoal_core)
target_include_directories(shoal_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_core PUBLIC Qt6::Core)
target_compile_definitions(shoal_core PRIVATE SHOAL_VERSION_STRING="${SHOAL_VERSION_TAG}")
shoal_set_warnings(shoal_core)

# assets: sprat sheets and their atlases, through sprat-lib.
add_library(shoal_assets STATIC
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
    editor/CanvasView.cpp
    editor/CanvasView.h
    editor/ClassesPanel.cpp
    editor/ClassesPanel.h
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
    editor/InspectorPanel.cpp
    editor/InspectorPanel.h
    editor/MainWindow.cpp
    editor/MainWindow.h
    editor/NewProjectDialog.cpp
    editor/NewProjectDialog.h
    editor/PointGrid.cpp
    editor/PointGrid.h
    editor/PresetsPanel.cpp
    editor/PresetsPanel.h
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
    editor/SheetBrowserPanel.cpp
    editor/SheetBrowserPanel.h
    editor/SheetItem.cpp
    editor/SheetItem.h
    editor/TileGridOverlay.cpp
    editor/TileGridOverlay.h
    editor/TransformHandle.cpp
    editor/TransformHandle.h
    editor/TransitionEdgeItem.cpp
    editor/TransitionEdgeItem.h
)
add_library(shoal::editor ALIAS shoal_editor)
target_include_directories(shoal_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_editor PUBLIC shoal::core shoal::assets Qt6::Widgets)
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
