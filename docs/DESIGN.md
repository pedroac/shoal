# Shoal design

Shoal is a visual scene graph and scene flow editor for indie developers who build
their own game engines. It is part of the Sprat ecosystem (MIT licensed): Sprat
packs sprites and their metadata, and Shoal places them, together with everything
else a scene needs, and declares how scenes follow one another.

This document is the specification the editor is built against. It records what
has been decided, and the open questions at the end record what has not.

## 1. Principles

**Shoal is not a game engine.** It is a declarative tool. Shoal is the Set
Designer, and the engine that reads its files is the Director. Shoal declares
*where*, *what* and *when*. The engine decides *how*.

- **Space belongs to Shoal.** Positions, sizes, marks, paths, regions, camera
  framing and UI anchors are authored visually, so no programmer hardcodes a
  coordinate or works out where a button goes.
- **Behavior belongs to the engine.** Gameplay logic, conditions, physics,
  pathfinding, conflict resolution and rendering stay in the engine. Shoal names
  them (an event, a requirement, an effect) and never evaluates them.
- **Spatial values are authoritative; feel values are hints.** An offset or a
  deadzone size is geometry, and the engine follows it. Smoothing, damping or a
  zoom limit is a hint the engine may ignore.
- **No runtime measurement.** Every size is declared or comes from sprat, so the
  layout math an engine needs is a few lines of C.
- **No expression evaluator.** Conditions are labels, selectors are term lists, and
  the runtime loader matches and reads, never interprets.
- **Resolve at export.** Whatever the editor can compute (layout at the design
  resolution, flattened structure, write sets, compiled selectors) is computed once
  at export, so engines stay simple.

## 2. Ecosystem and stack

| Part | Choice |
|---|---|
| Editor | Qt 6 Widgets, `QGraphicsView` for the flow graph and the scene canvas, C++20, CMake |
| Undo | `QUndoStack`; every model change is a command |
| Sprites | [sprat-lib](https://github.com/pedroac/sprat-lib) through `FetchContent`, pinned to a commit, with `-DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib` for local work (as sprat-raylib does) |
| Runtime loader (after v1) | `shoal-lib`, C99, no dependencies, modeled on sprat-lib |

This matches sprat-gui (Qt 6 Widgets, C++20, `QGraphicsView`, a WASM build), so
patterns and know-how carry over, and sprat-lib links directly with no FFI.

## 3. Conventions

- **Coordinates** are the same as sprat's: pixels, origin at the top-left, Y down,
  angles in degrees clockwise.
- **A world entity's position** is where its origin lands. For a sprite, the
  origin is its sprat pivot, so it is placed exactly as sprat-lib's drawing recipe
  places it.
- **Durations** are integer milliseconds (`duration_ms`).
- **Paths** are relative to the project root, use forward slashes, and must pass
  `sprat_path_is_safe()`: no absolute paths, no `..`.
- **Ids** are unique within their scope, and written as plain strings
  (`[A-Za-z_][A-Za-z0-9_-]*`).

## 4. Glossary

| Term | Meaning |
|---|---|
| Project | A folder with a `project.shoal.json` manifest |
| Flow | The graph of scenes and transitions, run as a stack |
| Scene | A set of layers and entities: a menu, a level, a cutscene, an overlay |
| Layer | An ordered slice of a scene, in `world` or `screen` space |
| Entity | Anything placed in a scene: a sprite, a mark, a region, a camera, a UI element |
| Class | A pure tag on an entity, for the engine's batch handling and for selectors |
| Mark | A named point: a spawn point, a stage mark, a placeholder |
| Path | A polyline or curve that actions can move along |
| Region | A shape the engine gives meaning to (`.trigger`, `.camera_bounds`) |
| Transition | An edge in the flow: a trigger, an effect and what persists |
| Modifier | An action list that runs on a host scene, blocking or in the background |
| Selector | A small query that picks entities: `#id`, `.class`, `layer()`, `group()` |
| Preset | An editor-only template that creates entities (copy-on-create) |
| Project template | An editor-only starting point for a new project |

## 5. Project on disk

```
my_game/
├── project.shoal.json          manifest
├── flow.json                   scene list, transitions, state keys
├── classes.json                optional class declarations
├── presets/
│   └── enemy_grunt.json        copy-on-create presets (editor-only)
├── scenes/
│   ├── level_1/
│   │   ├── scene.json          layers + entities
│   │   └── modifiers/
│   │       ├── altar_reveal.json
│   │       └── earthquake.json
│   └── pause/
│       └── scene.json
├── art/                        the user's assets, e.g. hero.sprat and its atlases
├── .shoal/                     per-user state, gitignored
│   ├── session.json            open tabs, zoom, selection, hidden/locked layers
│   └── autosave/
└── export/                     generated, gitignored
```

- **Source files are plain `.json`.** Only the manifest is marked, as
  `project.shoal.json`. `.shoal` is reserved for the raw export.
- **Ids come from paths and are never stored twice.** A scene's id is its folder
  name; a modifier's id is its file name, and its host is the scene folder it sits
  in. Renaming in the editor moves the file and updates every reference.
- **Modifiers have their own files**, so cutscene work and level layout don't
  collide in merges.
- **Shared editor state** (flow graph node positions, preset tints) lives under an
  `editor` key in the file it describes, and the export drops it. **Personal
  editor state** lives in `.shoal/session.json`, which is gitignored.
- **Folder projects only** in v1. Zipped projects may come in v2.

### Manifest

```jsonc
{ "format": "shoal-project", "version": 1,
  "name": "My Game",
  "design_resolution": [1920, 1080],
  "fit": "expand",
  "integer_scale": false,
  "start": "main_menu",
  "sheets": { "hero": "art/hero.sprat", "ui": "art/ui.sprat" },
  "export": { "dir": "export", "targets": ["raw", "json"], "bundle": false } }
```

Sprat sheets are declared once, under an alias. Entities refer to the alias, so
moving a `.sprat` file is a one-line change. Atlas images resolve relative to their
`.sprat` file, as in sprat-lib.

### Class declarations

```jsonc
// classes.json
{ "format": "shoal-classes", "version": 1,
  "classes": {
    "trigger":       { "doc": "Region; the engine fires an event on enter/exit", "color": "#e0a030" },
    "camera_bounds": { "doc": "Region; cameras stay inside the union of these",  "color": "#30a0e0" } } }
```

The class registry is derived from what the scenes use. Declarations are optional
documentation from the Set Designer to the Director; the editor shows them as
tooltips and autocompletes from them.

### Writing files

- **Deterministic.** Shoal writes its own JSON, not `QJsonDocument` (which sorts
  keys): a fixed key order per object type (`id`, `type`, `classes` first), arrays
  in their meaningful order (z-order), 2-space indent, a trailing newline, and
  numbers in their shortest round-trip form. Saving an unchanged project changes
  no bytes.
- **Merge-friendly.** Unordered collections, such as transitions and state keys,
  are written sorted by id.
- **Versioned.** Every file has `format` and `version`. The editor migrates older
  versions on load and refuses newer ones with a clear message. Migrations are
  tested with fixture files.

## 6. Flow

The flow is a **stack** of scenes. Transitions change it in one of four ways:

| `mode` | Effect | Example |
|---|---|---|
| `replace` | Swap the top scene | Menu → Level 1 |
| `push` | Put another scene on top; the one below becomes `paused`, `running` or `hidden` | Pause menu overlay |
| `push_modifier` | Run a modifier on the top scene | In-engine cutscene |
| `pop` | Return to what is underneath (no `to`) | Close the pause menu |

```jsonc
// flow.json
{ "format": "shoal-flow", "version": 1,
  "state": [ { "key": "inventory", "doc": "Carried between levels" },
             { "key": "player.hp" } ],
  "transitions": [
    { "id": "l1_to_l2", "from": "level_1", "to": "level_2", "mode": "replace",
      "trigger": { "event": "door_west_entered", "requires": ["has_key"] },
      "effect":  { "type": "fade", "duration_ms": 400, "easing": "ease_out", "params": {} },
      "persist": { "state": ["player.hp", "inventory"],
                   "entities": { "player": "spawn_west_door" } } },
    { "id": "l1_pause", "from": "level_1", "to": "pause", "mode": "push",
      "underlying": "paused",
      "trigger": { "event": "pause_pressed" } },
    { "id": "pause_close", "from": "pause", "mode": "pop",
      "trigger": { "event": "resume_pressed" } },
    { "id": "l1_altar", "from": "level_1", "mode": "push_modifier", "modifier": "altar_reveal",
      "trigger": { "event": "altar_reached" } } ],
  "editor": { "nodes": { "level_1": [120, 80], "level_2": [480, 80], "pause": [120, 300] } } }
```

- **Triggers are labels.** `event` is a name the engine fires; `requires` lists
  names of conditions the engine checks however it likes. Shoal records that the
  dependency exists and never evaluates it.
- **Effects are a vocabulary.** `fade`, `slide`, `iris` and others, with
  parameters. The editor previews the built-in ones; the engine implements them.
  Unknown effect types pass through unchanged.
- **State is declared by name.** Shoal says which keys carry over; their types and
  values belong to the engine.
- **Persisted entities map to marks.** A carried entity lands on a mark in the
  destination, chosen per transition, so entering through the west door and the
  east door can put the player in different places.
- **The start scene** is set in the manifest.

## 7. Scenes

```jsonc
// scenes/level_1/scene.json
{ "format": "shoal-scene", "version": 1,
  "size": [3840, 1080],
  "layers": [
    { "id": "bg",    "space": "world",  "parallax": [0.5, 0.5] },
    { "id": "world", "space": "world" },
    { "id": "hud",   "space": "screen" } ],
  "entities": [
    { "id": "camera", "type": "camera", "layer": "world", "classes": ["main"],
      "transform": { "pos": [960, 540] }, "zoom": 1, "bounds": ".camera_bounds" },
    { "id": "player", "type": "sprite", "layer": "world", "classes": ["actor"],
      "sprite": { "sheet": "hero", "path": "hero/idle.png" },
      "transform": { "pos": [320, 640], "rot": 0, "scale": [1, 1], "flip": [false, false] },
      "props": { "speed": 4.5 } },
    { "type": "sprite", "layer": "bg",
      "sprite": { "sheet": "hero", "path": "props/bush.png" },
      "transform": { "pos": [700, 650] } },
    { "id": "room_1_bounds", "type": "region", "layer": "world", "classes": ["camera_bounds"],
      "shape": { "rect": [0, 0, 1920, 1080] } } ] }
```

### Layers

- `space: "world"` layers move with the camera, scaled by `parallax` (default
  `[1, 1]`).
- `space: "screen"` layers are fixed to the viewport and use the UI layout model
  (section 11).
- Layer order is draw order. Within a layer, entity order is draw order.

### Entities

| Field | Meaning |
|---|---|
| `id` | Optional, unique in the scene. Needed only by entities something refers to |
| `type` | See the table below |
| `layer` | Required on top-level entities; children inherit their parent's layer |
| `classes` | Pure tags |
| `active` | Default `true`. An inactive entity is authored but not present until an action activates it |
| `visible` | Default `true` |
| `transform` | World layers: `pos`, `rot`, `scale`, `flip` |
| `layout` | Screen layers: anchors and offsets (section 11) |
| `props` | Typed custom key-values the engine reads (`bool`, `int`, `float`, `string`, `point`, `color`) |
| `children` | Child entities, with transforms relative to the parent |

| `type` | Contents |
|---|---|
| `empty` | Nothing but a transform; a pivot for children |
| `group` | A container with no visuals of its own |
| `sprite` | `{ sheet, path }`: a sprat sprite, bound by path |
| `animation` | `{ sheet, animation }`: a sprat animation, by name |
| `nineslice` | `{ sheet, path }` with a size; stretched with sprat's slice insets |
| `text` | A box with text, alignment and an overflow hint (see open questions) |
| `rect` | A plain colored box, mostly for UI |
| `mark` | A point, optionally with a facing direction |
| `path` | `{ points, curve: "linear" \| "bezier", closed }` |
| `region` | `{ rect }`, `{ polygon }` or `{ circle }` |
| `camera` | Section 10 |

Sprites are referenced by sheet alias plus sprite path, never by name, because sprat
names are basenames and repeat across folders. In the engine, the reference maps
directly onto `sprat_sheet_bind(SPRAT_BIND_SPRITE_PATH, …)`.

### Spatial primitives

Marks, paths and regions are ordinary entities with classes. Shoal declares them;
the engine gives them meaning. This parallels sprat: sprat puts markers on sprites,
Shoal puts marks in scenes.

### Placeholders

A persistent entity (such as `#player`) does not exist in the destination scene's
file. The destination has a mark for it instead, and the transition maps the entity
to that mark. The editor draws the mark as a **ghost**: the carried entity's sprite
from the source scene, semi-transparent, so the designer sees the real silhouette.

## 8. Selectors

| Term | Matches |
|---|---|
| `#id` | The entity with that id |
| `.class` | Entities with that class |
| `layer(name)` | Entities on that layer |
| `group(id)` | The descendants of the entity with that id |

- A selector is the **AND** of its terms: `layer(world).enemy`.
- A target list is the **OR** of its selectors: `["#hero", ".companion"]`.
- No property tests. Gameplay conditionals belong to the engine.
- The engine resolves selectors at run time, so they also match entities the engine
  creates, as long as those carry the classes.
- The export writes selectors as **compiled term lists** (and the source string,
  for readability), so the runtime matcher is a loop over terms.

## 9. Modifiers

A modifier is an action list that runs on its host scene: the scene whose folder
it lives in. A cutscene that halts gameplay, moves characters and then resumes is
the typical blocking modifier; an ambient earthquake is a typical background one.

```jsonc
// scenes/level_1/modifiers/altar_reveal.json
{ "format": "shoal-modifier", "version": 1,
  "blocking": true,
  "skippable": true,
  "priority": 0,
  "on_end": { "mode": "restore", "blend_ms": 600, "restore_follow": true },
  "actions": { "seq": [
      { "do": "unfollow",  "target": "#camera" },
      { "par": [
          { "do": "pan_to",  "target": "#camera", "to": "#mark_altar", "duration_ms": 2000, "easing": "ease_in_out" },
          { "do": "zoom_to", "target": "#camera", "zoom": 1.5, "duration_ms": 2000 },
          { "do": "move_to", "target": "#hero",   "to": "#mark_altar_front", "duration_ms": 2000 } ] },
      { "do": "shake",   "target": "#camera", "amplitude_px": 6, "duration_ms": 400 },
      { "do": "emit",    "event": "altar_revealed" } ] } }
```

### Execution

- `blocking: true` suspends the host's gameplay while the modifier drives; the scene
  still renders. `blocking: false` runs in the background alongside gameplay.
- `skippable: true` lets the engine skip to the end.
- `priority` is a hint for the engine's conflict resolution.
- **Action lists** are blocks: `seq` runs its children in order, `par` runs them
  together and ends when the last one ends. Blocks nest. Keyframe tracks may come
  in a later version.

### Actions

Every action has a target list (except `wait` and `emit`), and every timed action
declares `duration_ms`, or `speed` in pixels per second, from which the editor
derives a duration along the path length. `easing` is `linear` (the default),
`ease_in`, `ease_out` or `ease_in_out`; other names pass through to the engine.

| Action | Effect |
|---|---|
| `move_to { to }` | Move to a mark, an entity or a point |
| `move_along { path }` | Move along a path entity |
| `tween { prop, value }` | Change a numeric property over time |
| `set { prop, value }` | Change a property immediately |
| `play_animation { sheet, animation }` | Switch to a sprat animation |
| `set_visible { visible }` | Show or hide |
| `activate` / `deactivate` | Bring an authored, inactive entity into the scene, or take it out |
| `wait` | Wait for `duration_ms` |
| `emit { event }` | Fire an event name for the engine |
| Camera actions | Section 10 |

There is no `spawn` of new entities: presets are editor-only, so anything a
modifier brings in is authored in the scene as inactive and then activated, like a
stagehand bringing on a prop that was waiting in the wings.

### End states

**Every action has a well-defined end state.** Skipping a modifier means applying
the end states, so the engine never simulates what was skipped, and the editor can
scrub back and forth. For a target that moves, `move_to`, `pan_to` and `frame` read
its position **when the action starts** and don't track it afterwards.

### Ending

- `on_end.mode: "commit"` keeps everything as the modifier left it.
- `on_end.mode: "restore"` returns every property the modifier wrote to its value
  from before the modifier, over `blend_ms`. `restore_follow: false` leaves a
  camera's follow mode as the modifier left it, for cutscenes that intentionally
  end with a detached or static camera.

### Write sets

The export lists, per modifier, the (selector, property) pairs its actions write,
for example `#camera.{position, zoom, follow, shake_offset}` and `#hero.position`.
The engine resolves conflicts between concurrent modifiers from that, by
last-write-wins or by its own system priorities. The editor uses the same data to
warn when two modifiers that can run together write the same property.

## 10. Camera

The camera is an ordinary entity, `type: "camera"`, targeted by ordinary selectors
and actions. Each new scene starts with `#camera` tagged `.main`. A scene may have
more cameras (split-screen, a minimap); the editor flags a scene with no `.main`
camera, or with several.

| Property | Meaning |
|---|---|
| `transform.pos` | Center of the view, in world pixels |
| `transform.rot` | Degrees, clockwise |
| `zoom` | 1 shows the design resolution at 1:1 |
| `bounds` | A selector; the union of the matched regions constrains the view |
| `follow` | Optional `{ target, offset, deadzone }` |
| `zoom_min`, `zoom_max` | Optional hints; the engine sets the real limits |
| `damping` | Optional hint |

### Camera actions

Generic actions (`move_to`, `move_along`, `tween`) work on cameras too. These only
work on cameras:

| Action | End state |
|---|---|
| `pan_to { to, duration_ms, easing }` | Centered on the target |
| `cut_to { to }` | `pan_to` with duration 0 |
| `zoom_to { zoom, duration_ms, easing }` | The zoom value |
| `frame { targets, padding, duration_ms }` | A position and zoom that contain the targets |
| `follow { target, offset, deadzone }` | Follow mode set; finishes immediately |
| `unfollow` | The camera stays where it is |
| `set_bounds { selector }` | The new bounds |
| `shake { amplitude_px, duration_ms }` | No change: shake is transient |

- **Only `follow` tracks** a moving target.
- **Shake is additive.** It writes `shake_offset`, not `position`, so a background
  earthquake can shake the camera while a blocking cutscene pans it, with no
  conflict.
- **Bounds apply by default.** An action can opt out with `ignore_bounds: true`.
  Clamping is the engine's job; the editor's rehearsal clamps simply.

## 11. UI layout

Two questions, answered separately: how the design canvas maps onto a real screen
(the viewport fit), and how each element places itself in its parent (anchors).

### Viewport fit

Set in the manifest; a scene or layer may override it.

| `fit` | Behavior |
|---|---|
| `expand` (default) | Scale uniformly to fit the smaller axis; the canvas grows on the other axis (1920×1080 becomes 2520×1080 at 21:9) |
| `letterbox` | Scale uniformly and add bars |
| `crop` | Scale uniformly to cover; the edges get cut off |
| `stretch` | Scale each axis separately |

- `integer_scale: true` is a hint for pixel art: the engine snaps the scale to a
  whole number. The "Pixel Art" project template defaults to `letterbox` with
  `integer_scale: true`.
- World layers follow the same fit: with `expand`, a wider screen shows more of the
  world at zoom 1.

### Element layout

Entities on `screen` layers have a rect inside their parent's rect. A top-level
element's parent is the viewport, or the safe area.

```jsonc
"layout": {
  "anchor_min": [0, 0], "anchor_max": [0, 0],     // fractions of the parent rect
  "offset_min": [32, 32], "offset_max": [432, 80], // pixels from those anchor points
  "pivot": [0, 0],                                  // rotation/scale origin, fraction of own rect
  "area": "safe" }                                  // "viewport" (default) or "safe"
```

- **Equal anchors pin** the element to a point at a fixed size. **Different anchors
  stretch** it on that axis.
- Each corner resolves as `parent.min + anchor * parent.size + offset`.
- Rotation and scale are applied around the pivot, like a CSS `transform`, and
  never move siblings.
- `area: "safe"` lays out against the safe area; the engine supplies the actual
  insets (notches, overscan).
- A sprite with `size: "auto"` takes its sprat content size. A nine-slice
  stretches to its rect using its sprat slice insets.
- **The inspector speaks CSS.** An anchor preset grid (3×3 points, plus stretch
  rows, columns and full) switches between `left/top/width/height` and
  `left/right/top/bottom` fields, converted to and from min/max offsets.

### Not in v1

- **Containers.** A `stack` (direction, spacing, padding, alignment) may come after
  v1. It will be flattened at export into plain anchors and offsets, so engines
  never implement flexbox.
- **UI attached to world entities** (floating health bars, name tags). World to
  screen projection depends on the engine's camera, so it stays with the engine.

## 12. Presets and project templates

- **Presets** are editor-only and copy-on-create: a preset bundles a shape or
  subtree, default classes, default props and an editor tint. Creating from a
  preset copies plain entities with fresh ids; nothing stays linked, so the export
  never sees presets. Shoal ships Trigger Region, Camera Bounds, Spawn Mark and
  Camera, and projects add their own in `presets/`.
- **Linked prefabs are out of scope**: they need tree diffing and cross-file
  reference tracking, which conflicts with simple export structures.
- **Project templates** set manifest defaults and presets for a new project, for
  example "Default" (`expand`) and "Pixel Art" (`letterbox`, `integer_scale`).

## 13. The editor

- **Flow view (macro):** scenes as nodes, transitions as edges, modifiers as
  smaller nodes attached to their host scene. Double-click a scene to open it, an
  edge to edit the transition, a modifier to edit its action list.
- **Scene view (micro):** the canvas, with layers, a hierarchy tree, an inspector,
  a sprat sheet browser (drag sprites onto the canvas), selection and
  move/rotate/scale gizmos, a grid and snapping.
- **Transition editor:** trigger and requirements, the effect and its parameters,
  the persisted state, and the mapping of entities to marks.
- **Modifier editor:** the host scene's canvas with the action list underneath.
  Ghosts show start and end positions; paths are drawn on the canvas.
- **Rehearsal:** a play mode that steps through the flow and scrubs modifiers with
  linear tweens and sprat animations played in place. It is approximate on
  purpose; matching the engine's physics or timing is out of scope.
- **Look through camera:** shows the viewport as the player sees it, with parallax,
  screen layers and clamped bounds.
- **Viewport switcher:** shows the layout at 16:9, 21:9, 4:3, phone portrait and
  custom sizes, with a simulated safe area.
- **Problems panel:** errors, warnings and notes, each linked to the entity or file
  at fault:
  - A reference to a missing id, scene, modifier, mark or sheet; a dangling
    transition.
  - A carried entity with no mark mapped in the destination.
  - A selector that matches nothing in the editor (a note: the engine may create
    matching entities).
  - A class used but not declared, or used exactly once (a likely typo).
  - Two modifiers that can run together writing the same property.
  - A camera that starts outside its bounds; `follow` aimed at a selector matching
    several entities; a `frame` target larger than the view at `zoom_max` (when the
    hint is set); no `.main` camera, or several.
  - A UI element off-screen at a preset aspect, or under the simulated notch while
    its `area` is `viewport`.

### Sprat integration

A `SheetCache` follows sprat-lib's "load, bind, convert, free" pattern: it loads
each `.sprat` with `sprat_sheet_load_file`, copies sprites and animations into Qt
structures in the sheet's order, loads each atlas as a `QPixmap` relative to the
sheet's folder, and frees the sheet. A `QFileSystemWatcher` reloads a sheet when
sprat-gui or sprat-cli exports it again. The canvas draws sprites with sprat-lib's
drawing recipe (trim, pivot, packing rotation, flips).

## 14. Export

```
editor project (JSON, easy to diff)
        │ resolve: drop editor state, compute design rects,
        │ compile selectors, compute write sets, index references
        ▼
  export model  ← the specification
   ├── JSON writer  → for JS and scripting engines, and people reading it
   └── raw writer   → shoal-raw (.shoal), read by shoal-lib
```

- **Per-scene files by default:** `flow.shoal` plus `scenes/<id>.shoal`, so an
  engine loads and frees scenes on demand. `"bundle": true` in the manifest writes
  a single `game.shoal` instead. The JSON target mirrors the same structure.
- **What the export resolves:** ids from paths are written explicitly; sheet
  aliases become a sheet table that entities index into; screen elements carry
  their anchors, offsets, pivot and area **and** a precomputed `design_rect`;
  selectors are compiled term lists; each modifier carries its write set; editor
  data, presets and class docs are dropped.
- **shoal-raw** follows sprat-raw: a version on the first line (`shoal-raw 1`), one
  record per line (`flow`, `state`, `sheet`, `scene`, `layer`, `entity`,
  `transition`, `selector`, `modifier`, `action`), length-prefixed strings, totals
  declared before the records they count so a loader allocates one block, parents
  and references as indexes. The camera is an `entity` record and its actions are
  `action` records. The full specification will be written with the raw writer.

## 15. shoal-lib (after v1)

A C99 loader for `.shoal` files with no dependencies, built to sprat-lib's
standards: never crash on bad input, validate every index at load time, a
plain-struct API with a `shoal_` prefix, one allocation per loaded file, custom
memory functions. It will include:

- Loading `flow.shoal` and scene files, and queries by id.
- Selector matching over term lists.
- `shoal_layout_resolve(scene, viewport_w, viewport_h, safe_insets, out_rects)`,
  so engine authors don't rewrite the aspect-ratio math. Engines at a fixed
  resolution can use `design_rect` and skip it.

It stays independent of sprat-lib: sprite references are a sheet path and a sprite
path, which the engine binds with sprat-lib itself.

## 16. Code layout

```
shoal/
├── src/
│   ├── core/          QtCore only: model, commands, JSON reader/writer, migrations,
│   │                  validation (Problems), export model and writers
│   ├── assets/        sprat-lib wrapper: SheetCache, atlas pixmaps, sprite drawing
│   └── editor/        Qt Widgets application
│       ├── flow/      flow graph view
│       ├── scene/     canvas, layers, hierarchy, inspector, sheet browser
│       ├── transition/
│       ├── modifier/  action list editor, rehearsal
│       └── problems/
├── docs/              this design, the file formats
├── tests/             core tests with fixture projects, run headless
└── CMakeLists.txt
```

`core` depends only on QtCore, so the model, files, validation and export are tested
headless, and a command-line exporter can be added later without the GUI.

## 17. Roadmap to v1

1. **Skeleton:** CMake, CI (Linux, Windows, macOS), sprat-lib through FetchContent,
   `VERSION`, `AGENTS.md`, a test target, an empty main window.
2. **Core:** the model, commands and undo, deterministic JSON writing, project
   load/save, version checks, fixture projects with round-trip tests.
3. **Flow view:** add, move and remove scenes; draw transitions; save node
   positions.
4. **Scene view, world space:** layers, entities, the sprat sheet browser, sprite
   drawing and hot reload, gizmos, ids and classes, the class registry, presets.
5. **Spatial primitives:** marks, paths and regions; placeholders and ghosts; the
   Problems panel.
6. **Transitions:** triggers, effects, persistence, push/pop and overlays.
7. **Screen space:** anchors and offsets, the CSS-style inspector, fit modes, the
   viewport switcher.
8. **Modifiers and camera:** action lists, camera entities and actions, write sets,
   rehearsal, look through camera.
9. **Export:** the export model, JSON and raw writers, the format specification.

After v1: shoal-lib, a reference demo with sprat-raylib, containers, keyframe
tracks, a command-line exporter, a WASM build, zipped projects.

## 18. Open questions

- **Text:** literal strings, localization keys, or both? How are fonts referenced?
- **Audio:** stage-direction cues (`cue("door_creak")`) that the engine maps to
  files, or no audio in v1?
- **References across scenes:** can an id point into another scene
  (`level_2#door_west`), or does everything cross scenes through transitions only?
- **Events and requirements registry:** should `flow.json` declare event and
  requirement names with docs, as `classes.json` does for classes, so typos can be
  caught?
