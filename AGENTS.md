# AGENTS.md

Guidance for AI agents (and humans) working on **Shoal**.

## Purpose

Shoal is a visual scene graph and scene flow editor for custom game engines, part of
the Sprat ecosystem. It is **declarative, not a game engine**: Shoal is the Set
Designer that declares *where*, *what* and *when*, and the engine reading its export
is the Director that decides *how*.

`docs/DESIGN.md` is the specification. Read it before changing the model, the file
formats or the editor's behavior, and update it in the same commit when a decision
changes.

## Priorities

1. **Never lose or corrupt a user's project.** Loading tolerates bad input with a
   clear message; saving is deterministic, so an unchanged project saves to the same
   bytes.
2. **Keep the export simple for engines.** Resolve what the editor can resolve at
   export time. Don't add anything that needs an expression evaluator or runtime
   measurement in the engine.
3. **Stay portable.** It must build with GCC, Clang and MSVC, on Linux, Windows and
   macOS, with Qt 6.5 or later.

## Code layout

- `src/core/`: the model, commands, files, validation and export. **QtCore only**;
  never include Gui or Widgets headers here, so core stays testable headless.
  - `Model.h`: plain value types mirroring the source files; unmodeled keys live in
    each object's `extras`, so they survive a save.
  - `JsonReader` / `JsonWriter`: path-named format errors, and the canonical writer.
  - `ProjectCodec`: files to model and back, pure (no disk access), migrating older
    formats as it reads them (scene format 1, with layers and entity types, and
    format 2, with `align` and `cell`; manifest format 1, with a top-level `sheets`).
    `ProjectStore`: the disk side, atomic writes, skipping unchanged files, removing
    stale ones.
  - `CommandStack`: undo history, like `QUndoStack` (which is in QtGui).
    `ProjectDocument`: the open project, its mutators and change signals.
    `FlowCommands`: the commands that change scenes and transitions, effects included.
    `TransitionTypes`: transition types, read from their files when used: the built-in
    ones (`assets/transitions/`, built into core as resources) and the project's own
    (`transitions/`); and effects made from them. `TransitionTypeCommands`: adding,
    customizing, renaming and removing them.
    `SceneCommands`: the commands that change entities, which `SceneTree`
    addresses by `EntityPath`. `SceneTree` also places entities: their rects and
    pivots, positions and sizes in px, % or tiles against their parent's `Frame`,
    tiled containers' grids and tints, with `ImageMetricsFn` for what only the
    sheets know.
  - `ProjectTemplates`: new projects and scenes. `Presets`: the built-in presets, and
    reading a preset into entities with fresh ids (presets stay as written in the
    model). `ClassCommands`: declaring classes in classes.json. `PresetCommands`:
    adding, customizing, renaming and removing presets; the document edits a
    preset's entities as a tree of their own (`entityTree()`, `presetTreeId()`), so
    the entity commands work on presets too. `AssetCommands`: declaring assets
    (sheets, images, fonts, audio, video) in the manifest, each kind by alias.
- `src/assets/`: sprat sheets and atlases, through sprat-lib, and standalone images.
  The only place that includes `<sprat/sprat.h>`.
  - `SpriteSheet`: `loadSheet()` copies a sheet into Qt types, and `placeSprite()`
    is sprat-lib's drawing recipe; `loadImageAsSheet()` makes a standalone image a
    sheet of one sprite, so it draws, tints and sizes as a sprite does. `SheetCache`: a
    project's sheets and standalone images by alias, with hot reload. `FontCache`: its
    fonts by alias, registered with `QFontDatabase` from their files, with hot reload.
- `src/editor/`: the Qt Widgets application, built as a library (`shoal_editor`)
  so tests can create its windows. `main.cpp` is the executable's only source.
  - `MainWindow`: one open `ProjectDocument`, the menus, New/Open/Save, and the
    tabs (`WorkspaceTabs`: the tab bar over the project panel, the workspace and the
    context panel): the flow, the presets (`PresetsView`: a `SceneView` of the preset
    current in `PresetsPanel`), then the scenes. The project panel holds the scenes list
    on the flow, the presets and assets lists elsewhere; the context panel the scene
    settings on the flow, or the entity tree over the inspector. Methods that take a path
    and don't show dialogs (`openProject`, `saveProjectAs`) are what tests call; the
    slots wrap them with dialogs.
  - `FlowScene`: the flow graph's items, kept in line with the document by its
    signals, and the edits as commands. `FlowView` shows it (zoom, pan, context
    menus). `SceneNodeItem` and `TransitionEdgeItem` are its items.
  - `SceneCanvas`: one scene's entities (`EntityItem`), likewise following the
    document; it rebuilds its items when the tree's shape changes and updates them
    in place otherwise, since an item may be inside its own mouse handler.
    `SceneView` is its tab, `SceneTreePanel` its entity tree.
    `TileGridOverlay` draws the tiled containers' grids above them, when they help.
    `CanvasView` holds the zoom and pan both views share. `TextLayout` lays a text
    part out as the editor previews it.
  - `AssetBrowserPanel`: the project's assets, by kind: sheets with their sprites and
    animations, and standalone images, as thumbnails (`Thumbnails`); fonts, audio and
    video by alias. It only browses: a drag or a double-click hands an `AssetItem` to
    `SceneView`, which places it, and a drag onto an `AssetField` gives it to the
    selected entity.
    `PresetsPanel` does the same with presets, and adds, duplicates and removes them.
    Both tell a click from a drag (`ClickOrDragView`), so a drag leaves the current
    row. `InspectorPanel` edits the selected
    entity, with unit boxes for positions and sizes, a `PointGrid` for pivots and a
    `ColorButton` for tints and fills, or, with none selected, the top level's scene
    settings (a `SceneSettingsPanel`), or a preset's (a `PresetSettingsPanel`). Its
    `AssetField` chooses the entity's asset, whose kind makes it an image, a video, a text or a sound:
    dropped, typed with a completer, browsed or cleared. A preset's entities have a Locks tab too,
    for the properties they can't change in a scene (`core::Lock`, kept by `keepingLocked()`
    in `SceneTree`, applied by `SceneCanvas::keepingLocks()` to every edit).
  - `Icons`: the editor's own icons, the SVGs in `assets/icons/` built in as resources
    under short names; the plain ones take the palette's colors. `Splitter` shows the
    drag handle's dots on its handles. `ProjectDropArea` is what the window shows with no
    project open, and opens a project folder dropped on it.
  - `TransitionPanel`: the context panel for the flow's selected transition: its id,
    mode and destination, trigger, effect (a field for each option its type's file
    declares) and what it carries. `FlowScene` selects a transition without an edge
    on its node's line (`selectedExit()`). `TransitionTypesPanel` lists the
    transition types, on the flow, and `TransitionTypePanel` edits one's file.
  - Views never change the model directly: they push commands, and redraw from
    the document's signals, so undo and redo need no special handling.
- `tests/`: one Qt Test executable per area, registered with CTest and run on the
  offscreen platform. `tests/fixtures/basic/` is a project in canonical form: loading
  and saving it must reproduce every byte, so a change to the writer or the format
  shows up there. `tests/fixtures/basic_format1/` is the same project in the formats
  before (scene format 1); reading it must give `basic`'s bytes, plus the scene format 1
  can't express. A new or changed error message goes in `ProjectCodecTests`' table,
  exactly. `tests/fixtures/sheets/` is a hand-written sprat sheet and atlas, described
  in its README. Editor tests drive views with real mouse events (`QTest::mousePress`
  and friends on the viewport) where the behavior is about the mouse.
- `tools/`: developer scripts, such as `make_transition_previews.py`, which draws the
  built-in transition types' preview GIFs (Pillow and numpy).
- `docs/`: the design specification, and the file format specifications as they
  are written.

## Dependencies

- **Qt 6** (Core, Gui, Svg, Widgets, Test).
- **sprat-lib** through `FetchContent`, pinned to a commit in `CMakeLists.txt`. To
  build against a local checkout, pass `-DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib`.
  Follow its "load, bind, convert, free" pattern: copy what the editor needs out of a
  sheet, then free it.

## Coding practices

- C++20. Namespaces follow the folders: `shoal::core`, `shoal::assets`,
  `shoal::editor`.
- Qt conventions: `m_` prefix for members, `UPPER_CASE` constants, signals named
  `<property>Changed()`, slots named `on<Signal>()` or `handle<Event>()`.
- Files are named after the class they hold, in PascalCase, with `#pragma once`.
- Every model change goes through an undo command.
- Format with the repository's `.clang-format`.
- Keep the build warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
  (`/W4` on MSVC).

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DSHOAL_SANITIZE=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add or update tests for every behavior change or bug fix.

## Versioning and commits

- Update `VERSION` (`vMAJOR.MINOR.PATCH`) in the same commit as the change.
- Commit messages: `feat: ...`, `fix: ...`, `docs: ...`, `test: ...`, `refactor: ...`.
- Never commit build output.
