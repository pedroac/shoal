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
  - `ProjectCodec`: files to model and back, pure (no disk access). `ProjectStore`:
    the disk side, atomic writes, skipping unchanged files, removing stale ones.
  - `CommandStack`: undo history, like `QUndoStack` (which is in QtGui).
    `ProjectDocument`: the open project, its mutators and change signals.
    `FlowCommands`: the commands that change scenes and transitions.
  - `ProjectTemplates`: new projects and scenes.
- `src/assets/`: sprat sheets and atlases, through sprat-lib. The only place that
  includes `<sprat/sprat.h>`.
  - `SpriteSheet`: `loadSheet()` copies a sheet into Qt types, and `placeSprite()`
    is sprat-lib's drawing recipe. `SheetCache`: a project's sheets by alias, with
    hot reload.
- `src/editor/`: the Qt Widgets application, built as a library (`shoal_editor`)
  so tests can create its windows. `main.cpp` is the executable's only source.
  - `MainWindow`: one open `ProjectDocument`, the menus, New/Open/Save. Methods
    that take a path and don't show dialogs (`openProject`, `saveProjectAs`) are
    what tests call; the slots wrap them with dialogs.
  - `FlowScene`: the flow graph's items, kept in line with the document by its
    signals, and the edits as commands. `FlowView` shows it (zoom, pan, context
    menus). `SceneNodeItem` and `TransitionEdgeItem` are its items.
  - Views never change the model directly: they push commands, and redraw from
    the document's signals, so undo and redo need no special handling.
- `tests/`: one Qt Test executable per area, registered with CTest and run on the
  offscreen platform. `tests/fixtures/basic/` is a project in canonical form: loading
  and saving it must reproduce every byte, so a change to the writer or the format
  shows up there. A new or changed error message goes in `ProjectCodecTests`' table,
  exactly. `tests/fixtures/sheets/` is a hand-written sprat sheet and atlas, described
  in its README. Editor tests drive views with real mouse events (`QTest::mousePress`
  and friends on the viewport) where the behavior is about the mouse.
- `docs/`: the design specification, and the file format specifications as they
  are written.

## Dependencies

- **Qt 6** (Core, Gui, Widgets, Test).
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
