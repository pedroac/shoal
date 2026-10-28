# core: the model, files, validation and export. QtCore only, so it is tested headless.
add_library(shoal_core STATIC
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
    core/ProjectTemplates.cpp
    core/ProjectTemplates.h
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
    assets/SpratLibrary.cpp
    assets/SpratLibrary.h
)
add_library(shoal::assets ALIAS shoal_assets)
target_include_directories(shoal_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shoal_assets PUBLIC shoal::core Qt6::Gui PRIVATE sprat::sprat)
shoal_set_warnings(shoal_assets)

# editor: the Qt Widgets application, as a library so tests can create its windows.
add_library(shoal_editor STATIC
    editor/FlowScene.cpp
    editor/FlowScene.h
    editor/FlowView.cpp
    editor/FlowView.h
    editor/MainWindow.cpp
    editor/MainWindow.h
    editor/NewProjectDialog.cpp
    editor/NewProjectDialog.h
    editor/SceneNodeItem.cpp
    editor/SceneNodeItem.h
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
