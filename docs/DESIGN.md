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
| Undo | `core::CommandStack`, modeled on `QUndoStack` (which is in QtGui, while core uses QtCore only); every model change is a command |
| Sprites | [sprat-lib](https://github.com/pedroac/sprat-lib) through `FetchContent`, pinned to a commit, with `-DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib` for local work (as sprat-raylib does) |
| Runtime loader (after v1) | `shoal-lib`, C99, no dependencies, modeled on sprat-lib |

This matches sprat-gui (Qt 6 Widgets, C++20, `QGraphicsView`, a WASM build), so
patterns and know-how carry over, and sprat-lib links directly with no FFI.

## 3. Conventions

- **Coordinates** are the same as sprat's: pixels, origin at the top-left, Y down,
  angles in degrees clockwise.
- **An entity's position** is where its origin lands in its parent, on each axis
  in pixels from the parent's origin, in percent of the parent's rect, or on one of
  its tiles, with an offset in pixels (section 7). A rect is placed around the
  origin by its `pivot`;
  an image with an automatic size and pivot puts its sprat pivot on the origin, so
  it is placed exactly as sprat-lib's drawing recipe places it.
- **A rect with no width or height** is a point on that axis. Where the editor
  would divide by its size (a fraction of it), it uses 0.
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
| Scene | A tree of entities: a menu, a level, a cutscene, an overlay |
| Entity | A node in a scene's tree: a transform, children, and the parts it needs |
| Part | What an entity has besides its transform: an image, a rect, a view, a layout, ... |
| Layer | Informally, an entity near the top of the tree that sets `space` or `parallax` |
| Class | A pure tag on an entity, for the engine's batch handling and for selectors |
| Mark | A named point: a spawn point, a stage mark, a placeholder |
| Path | A polyline or curve that actions can move along |
| Region | A shape the engine gives meaning to (`.trigger`, `.camera_bounds`) |
| Transition | An edge in the flow: a trigger, an effect and what persists |
| Modifier | An action list that runs on a host scene, blocking or in the background |
| Selector | A small query that picks entities: `#id`, `.class`, `group()` |
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
│   │   ├── scene.json          the entity tree
│   │   └── modifiers/
│   │       ├── altar_reveal.json
│   │       └── earthquake.json
│   └── pause/
│       └── scene.json
├── art/                        the user's assets, e.g. hero.sprat and its atlases
├── .shoal/                     per-user state, gitignored
│   ├── session.json            open tabs, zoom, selection, hidden/locked entities
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
- **Shared editor state** (flow graph node positions, preset swatches) lives under an
  `editor` key in the file it describes, and the export drops it. A flow node's
  position is its top-left corner; the editor stores whole numbers. **Personal
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
  keys): a fixed key order per object type (`format` and `version` first, then
  `id`, `classes`, ...), arrays in their meaningful order (z-order),
  2-space indent, `\n` line endings, a trailing newline, UTF-8 without escaping
  non-ASCII text, and numbers in their shortest round-trip form. Arrays of scalars
  are written on one line (`"pos": [320, 640]`), other arrays one element per
  line. Saving an unchanged project changes no bytes, and touches no files.
- **Short scene files.** In scenes and modifiers, a value equal to its default is
  left out (`active: true`, `rot: 0`, `space: "world"`, `parallax: [1, 1]`,
  `mode: "stretch"`, `tint: "#ffffff"`, `tint_mode: "mix"`, an empty `classes`).
  Settings files (the manifest) write every value, so they document themselves.
- **Merge-friendly.** Unordered collections, such as transitions and state keys,
  are written sorted by id, and maps sorted by key.
- **Nothing is lost.** Keys the model doesn't know are kept and written back, after
  the known ones, sorted. This keeps hand-added annotations, and lets data the
  editor doesn't model yet (sprite references, shapes, camera settings, layouts,
  modifier actions) round-trip until the roadmap step that models it.
- **Versioned.** Every file has `format` and `version`. The editor migrates older
  versions on load and refuses newer ones with a clear message. Migrations are
  tested with fixture files. Opening a project with older files doesn't mark it
  modified; the next save writes them in the current version, the one time an
  unchanged project's bytes change.

### Reading files

- **A file fails to load only when the model can't represent it:** bad JSON, wrong
  types, unknown enum values, invalid ids, transitions whose fields contradict their
  mode, unsupported versions. Messages name the value at fault by its path, quote
  what was found and say what was expected, like sprat-lib's:
  `scenes/level_1/scene.json: entities[2].transform.flip: expected [horizontal, vertical], each true or false, found an array`.
  Syntax errors carry a line and column.
- **References that don't resolve still load**, such as a transition to a missing
  scene or an image from an undeclared sheet. They are the Problems panel's to report,
  so a hand edit never locks a user out of their project.
- Each broken file is reported; a project opens only when every file reads.
- `project.shoal.json` and `flow.json` are required; `classes.json` and `presets/`
  are optional. A modifier whose scene folder has no `scene.json` is an error.
- **Saving removes only what Shoal loaded or wrote before**, when the project no
  longer has it (a deleted or renamed scene, for example), along with folders left
  empty. Other files in the project folder, such as art or notes, are never
  touched. Each file is written atomically.
- **A rename that only changes case** (`Boss` to `boss`) renames the folder or
  file on disk before writing. On case-insensitive filesystems (Windows, macOS)
  both spellings are the same file, so writing then removing the old spelling
  would delete the scene.
- **Save As into a folder that holds a project replaces it**, after asking: the
  old project's files that the new one doesn't have are removed, so none of its
  scenes linger. Other files there are kept.

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
  east door can put the player in different places. It becomes the mark's next
  sibling, at the mark's position, so it is drawn where the mark is and inherits
  what the mark inherits (space, parallax, tint).
- **The start scene** is set in the manifest.

## 7. Scenes

A scene is a tree of entities. There are no layers and no entity types: every
entity has the same few fields, and the parts it has (an image, a rect, a view of
the scene) say what it is. A sprite is an entity with an `image`; a group is one
with nothing but children; a background layer is one that sets `parallax` and holds
the background's entities.

```jsonc
// scenes/level_1/scene.json
{ "format": "shoal-scene", "version": 3,
  "size": [3840, 1080],
  "entities": [
    { "id": "bg", "parallax": [0.5, 0.5], "children": [
        { "image": { "sheet": "hero", "sprite": "props/bush.png" },
          "transform": { "pos": [700, 650] } } ] },
    { "id": "world", "children": [
        { "id": "camera", "classes": ["main"],
          "transform": { "pos": [960, 540] }, "size": [1920, 1080], "pivot": [0.5, 0.5],
          "view": { "zoom": 1, "bounds": ".camera_bounds" } },
        { "id": "player", "classes": ["actor"],
          "image": { "sheet": "hero", "sprite": "hero/idle.png" },
          "transform": { "pos": [320, 640], "rot": 0, "scale": [1, 1], "flip": [false, false] },
          "props": { "speed": 4.5 } },
        { "id": "room_1_bounds", "classes": ["camera_bounds"], "size": [1920, 1080] } ] },
    { "id": "hud", "space": "screen", "children": [
        { "id": "health", "area": "safe",
          "transform": { "pos": [32, 32] }, "size": [400, 48],
          "image": { "sheet": "ui", "sprite": "panel.png", "mode": "nineslice" } },
        { "id": "minimap", "area": "safe", "at": ["100%", "0%"], "pivot": [1, 0],
          "transform": { "pos": [-32, 32] }, "size": [240, 160],
          "image": { "sheet": "ui", "sprite": "frame.png", "mode": "nineslice" },
          "tint": "#ffffffc0" } ] } ] }
```

### Entities

| Field | Meaning |
|---|---|
| `id` | Optional, unique in the scene. Needed only by entities something refers to |
| `classes` | Pure tags |
| `active` | Default `true`. An inactive entity is authored but not present until an action activates it; its descendants aren't either |
| `visible` | Default `true`. A hidden entity hides its descendants |
| `transform` | `pos`, `rot`, `scale`, `flip`, relative to the parent |
| `props` | Custom key-values the engine reads; typed (`bool`, `int`, `float`, `string`, `point`, `color`) once the open question on prop types is settled |
| `children` | Child entities |

- **Draw order is the tree's order**, depth first: an entity, then its children,
  then its next sibling.
- **What an entity is to the engine** is said by its classes and its parts. A mark
  is an entity with only a transform (its `rot` is its facing), a region one with a
  rect or a shape, a path one with a `path`. Shoal declares them; the engine gives
  them meaning. This parallels sprat: sprat puts markers on sprites, Shoal puts
  marks in scenes.

### Parts

| Part | Meaning |
|---|---|
| `at` | Where `pos` is measured from, on each axis: `0`, the parent's origin (the default), a percentage of the parent's rect such as `"50%"`, or a tile of a tiled parent such as `"3□"` (see units below) |
| `size`, `pivot` | A rect of `size` [w, h] around the origin, each side in pixels, a percentage of the parent's rect (`"50%"`) or a number of its tiles (`"2□"`). `pivot` is the fraction of the rect that sits on the origin. Both are automatic when left out: an image's, from its sprite (see images below), or for an entity without an image no rect and a pivot of `[0, 0]`, the top-left corner. Rotation and scale turn about the origin |
| `area` | `"viewport"` (the default) or `"safe"`: the screen rect an entity measures or lays out against when its parent has no rect, in screen space |
| `image` | `{ sheet, sprite }`, a sprat sprite by path, or `{ sheet, animation }`, a sprat animation by name, with a `mode`; it fills the rect |
| `fill` | A color, `#rrggbb` or `#rrggbbaa`, filling the rect |
| `tint`, `tint_mode` | A color, `#rrggbb` or `#rrggbbaa` (default `#ffffff`), multiplying the entity's image, fill and text and passed down to its descendants; `tint_mode` says how it combines with the parent's (see tint below) |
| `clip` | `true` draws the entity and its descendants only inside its rect |
| `container` | How it places its children: `{ "type": "tiled", "tile_size": [w, h], "tile_pivot": [x, y] }` lays a grid of tiles over its rect (see containers below). Left out, they are placed freely |
| `view` | Shows the scene through the rect: a camera (section 10) |
| `space`, `parallax`, `fit` | How the entity and its descendants follow the camera (below); `fit` goes with `space` |
| `layout` | Places the rect inside the parent's, by anchors (section 11), in place of `pos`, `at` and `size` |
| `shape` | An area that isn't a rect: `{ "polygon": [[x, y], ...] }` or `{ "circle": r }` |
| `path` | `{ points, curve: "linear" \| "bezier", closed }` |
| `text` | Text in the rect, with alignment and an overflow hint (see open questions) |

Parts that need a rect (`fill`, `clip`, `view`, `text`, `container`) do nothing without
one, and the Problems panel reports them.

### Units

A position and a size have a unit on each axis:

| Unit | Written | A position is | A size is |
|---|---|---|---|
| px | a number: `32` | from the parent's origin (`at` is `0`) | that many pixels |
| % | `"50%"` | from the parent's rect: `0%` its left or top edge, `100%` its right or bottom | that share of the parent's rect |
| □ | `"3□"` | on that tile of a tiled parent (containers below) | that many tiles |

```jsonc
"at": ["100%", "0%"], "transform": { "pos": [-32, 32] }, "pivot": [1, 0],
"size": ["25%", 48]
```

- **`pos` is the pixels, always.** On an axis in pixels it is the position; on an
  axis in percent or in tiles it is an offset from the point `at` names. The
  example keeps the entity's top-right corner 32 pixels in from its parent's,
  whatever the parent's size, a quarter of its width, and 48 pixels tall.
- Each origin resolves, per axis, as `parent.origin + pos` in pixels,
  `rect.min + at / 100 * rect.size + pos` in percent, and
  `rect.min + (at + tile_pivot) * tile_size + pos` in tiles.
- **What a parent offers:** percentages need a rect, and tiles a tiled container
  with one. The top level measures against the scene's rect, from the origin to
  its `size`; in screen space, against the screen's rect (the viewport, or the safe
  area with `area: "safe"`). A unit the parent doesn't offer counts as 0 there, a
  position from the origin and a size of nothing, and the Problems panel will
  report it.
- **A percentage pins; a layout stretches with margins.** A position and a size in
  percent follow the parent's size, but a size has no offset, so a `layout`
  (section 11) is still what keeps an entity 32 pixels in from each edge.
- The export writes each position and size resolved in pixels against its parent
  as authored, and keeps the units, since a parent's size can change at run time;
  in screen space it also carries the precomputed `design_rect`. An engine that
  never changes sizes reads the pixels and ignores the units.

### Containers

A container says how an entity places its children. Without one they are placed
freely, each by its own `pos` and `at`. The one type so far is `tiled`, for
tilemaps, inventories and boards; a `stack` may come later (section 11).

```jsonc
"container": { "type": "tiled", "tile_size": [64, 64], "tile_pivot": [0.5, 1] }
// and in each child
"at": ["3□", "1□"], "transform": { "pos": [0, -8] }
```

- **The grid covers the container's rect**, from its top-left corner, with as many
  whole tiles as fit: a 256×128 rect of 64×64 tiles has 4 columns and 2 rows, so
  the number of tiles follows the container's size. `tile_size` is required, each
  side more than 0.
- **A child in tiles lands on its tile's `tile_pivot` point, plus `pos`.** Its
  pivot sits on its origin, so `tile_pivot: [0.5, 0.5]` (the default) centers each
  child's pivot in its tile, and `[0.5, 1]` stands sprites on the tile's bottom
  edge. A tile is a whole number from 0, counted from the top-left one; `pos` is
  usually `[0, 0]`, and nudges a child within its tile.
- **Each axis has its own unit**, so a child can sit in a column, in tiles, at a
  height in pixels or percent. A child the editor places in a tiled container is
  in tiles on both axes. A tile outside the grid is still placed by the formula,
  so a container that shrinks keeps its children's tiles; the Problems panel will
  report them.
- A size in tiles is that many of the parent's tiles: `["2□", "1□"]` covers two
  side by side.
- A tiled container with no rect has no tiles, and places its children freely.
- **What moves with what:** changing the tile size, the tile pivot or the
  container's size keeps the children's tiles, so they move with the grid. Making
  an entity a tiled container, or a free one, keeps its children where they are
  drawn, at the size they have: each goes on the tile nearest to it on both axes,
  the rest kept in `pos`, or, freed, is measured in pixels where it was in tiles.
  Moving an entity into a tiled container does the same.
- The export writes each child's position resolved against its tile, and keeps
  the tiles and the container, as for the other units: an engine that works on a
  grid reads the tiles, and any other engine reads the positions.

### Tint

An entity draws its image, fill and text multiplied by its **effective tint**, as
raylib's `tint` and SDL's color and alpha mods do, and its children receive that
effective tint. Each entity says, with `tint_mode`, how its own `tint` combines with
the one it receives:

| `tint_mode` | Effective tint | For |
|---|---|---|
| `mix` (default) | The received tint multiplied by its own, channel by channel | Fading or coloring a whole subtree from any entity in it, image or not |
| `inherit` | The received tint; its own is ignored | Following the parent exactly, whatever it was given |
| `override` | Its own; the received one is ignored | Opting out: with the default white, it draws untinted |

- A top-level entity receives white.
- An entity with no image is still a link in the chain, so a group tints its
  descendants without drawing anything.
- To tint only an entity's own image, its children set `override`.
- Tints can change at run time (`tween` on `tint`), so the export writes each
  entity's tint and mode as they are, and the engine works the effective tint out
  while it draws, parents first: one multiply per entity.

### Images

- **An image can change** without the entity changing: replacing or removing it
  keeps the entity's id, classes, transform and children.
- **An image fills its entity's rect.** `mode` says how: `stretch` (the default)
  scales it to the rect, and `nineslice` fills the rect using sprat's slice insets.
- **Its size and pivot are automatic** until the entity sets its own: the sprite's
  natural size (the original image, trim included), and its sprat pivot at the
  same fraction of the rect as of the image. So an image with both automatic is
  drawn exactly as sprat draws the sprite, and follows it when the sheet is packed
  again or the image changes. For an animation they follow the current frame,
  which sprat gives the engine. Setting a `size` stretches the image to it; the
  automatic pivot stays the sprite's until a `pivot` is set too.
- The export writes the automatic size and pivot out for sprites, and marks them
  automatic for animations.
- **A nine-slice's** borders keep their size, shrinking proportionally when they
  don't fit, and the edges and the middle stretch, as in sprat-raylib's
  `DrawSpratFrameNPatch`; slices are cut in the original image, then clipped to the
  trimmed content. Its `scale` scales all of it, borders included.

Sprites are referenced by sheet alias plus sprite path (`"image": { "sheet": "hero",
"sprite": "hero/idle.png" }`), never by name, because sprat names are basenames and
repeat across folders. In the engine, the reference maps directly onto
`sprat_sheet_bind(SPRAT_BIND_SPRITE_PATH, …)`.

### Space and parallax

- `space: "world"` (the default) moves with the camera, scaled by `parallax`
  (default `[1, 1]`). `space: "screen"` stays fixed to the viewport, and its
  entities place themselves in percent of it, or by a `layout` to stretch
  (section 11).
  `fit`, set with `space`, overrides the scene's viewport fit there.
- An entity's descendants inherit its space and parallax.
- **They are set near the top of the tree.** An entity may set `space`, `parallax`
  or `fit` only when each of its ancestors sits at its own parent's origin (an
  identity transform, `at` the origin, and no `layout`) and sets none of them
  itself;
  otherwise the engine would have to carry camera offsets through moved parents at
  run time. A file can say it anyway: the
  Problems panel reports it as an error, and the export refuses the scene.

### Scene format 1

Version 1 scene files, with layers and entity types, are migrated on load and saved
as version 3:

- Each layer becomes a top-level entity with its id, `space`, `parallax` and `fit`,
  holding the top-level entities that named it, in order. When an entity already
  has the layer's id, the layer's entity takes the next free one (`world_2`). An
  entity naming a missing layer stays at the top level, after them, keeping its
  `layer` as an unknown key for the Problems panel to report.
- A `sprite` or an `animation` becomes an `image` with an automatic size and pivot;
  a `nineslice`, an image in `nineslice` mode that keeps its size, its pivot left
  automatic as format 1 placed it.
- A `region`'s `rect: [x, y, w, h]` becomes its `size`, with the pivot that keeps
  the rect where it was (`[-x / w, -y / h]`). On an axis with no size, the pivot is
  0 there, and the offset moves into the entity's position instead, its children
  moved back by as much, so nothing moves. A polygon or a circle becomes its
  `shape`. A `rect` keeps its size, and its color becomes its `fill`.
- A `layout`'s `pivot` and `area` move to the entity.
- A `camera`'s settings become its `view`, with the design resolution as its size
  and its pivot at the center, since its position was the view's center.
- A `path`'s and a `text`'s settings become those parts. `empty`, `group` and
  `mark` need nothing. An unknown type is kept as written.

Modifier files keep their version: on load, the `layer(x)` terms of their
selectors are rewritten as `group(x)`, naming the entity the layer became.

The `basic` fixture is in format 3; a format 1 copy of it tests the migration.

### Scene format 2

Version 2 scene files, and version 2 presets, placed entities by `align`, fractions
of the parent's rect, and by `cell` in a tiled container. They are migrated on load
and saved as version 3:

- An `align` becomes `at` in percent (`[1, 0]` is `["100%", "0%"]`), its `pos` the
  offset. At the top level, and in a tiled container, an `align` did nothing, and
  is dropped; a preset's entities are taken as children of the parent they are
  added to.
- A child of a tiled container is in tiles on both axes, its `cell`'s, or tile
  `[0, 0]` without one. Elsewhere a `cell` did nothing, and is dropped.

### Editing scenes

- **Entities are addressed by their path in the tree** (indexes from the top level
  down), since ids are optional. Commands record paths; the undo history is linear,
  so they stay valid.
- **Renaming an entity's id renames it in the transitions** that carry it out of the
  scene (`persist.entities` keys) or land something on it (`persist.entities`
  values). Selectors in modifiers and camera settings are not rewritten yet; the
  Problems panel will report what they no longer match.
- **Removing an entity** removes its descendants with it, in one undo step.
- **Moving an entity under another parent** keeps it where it is drawn, at the
  size it has: its position, angle and scale are recalculated relative to the new
  parent, in the units it has where the new parent offers them (in a tiled
  container, on the tile nearest to it), in pixels otherwise, and its children
  follow it. A skew, which a parent's rotation and uneven scale could give
  it, can't be expressed and is dropped; flips are the entity's own and don't
  change.
- **Adding or removing a part** is one undo step, and so is replacing an image.
- **Changing the unit of a position or a size, or the `pivot`,** keeps the entity
  where it is drawn, at its size: a position goes to a percentage with no offset,
  or to the nearest tile with the rest as its offset, or to pixels.

### Placeholders

A persistent entity (such as `#player`) does not exist in the destination scene's
file. The destination has a mark for it instead, and the transition maps the entity
to that mark. The editor draws the mark as a **ghost**: the carried entity's image
from the source scene, semi-transparent, so the designer sees the real silhouette.

## 8. Selectors

| Term | Matches |
|---|---|
| `#id` | The entity with that id |
| `.class` | Entities with that class |
| `group(id)` | The descendants of the entity with that id |

- A selector is the **AND** of its terms: `group(world).enemy`.
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
- When left out, `blocking` is `true` and `skippable` is `false`; Shoal always
  writes both.
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
| `tween { prop, value }` | Change a number, a point or a color over time; points and colors channel by channel |
| `set { prop, value }` | Change a property immediately |
| `set_image { sheet, sprite }` | Switch to a sprat sprite |
| `play_animation { sheet, animation }` | Switch to a sprat animation |
| `set_visible { visible }` | Show or hide |
| `activate` / `deactivate` | Bring an authored, inactive entity into the scene, or take it out |
| `wait` | Wait for `duration_ms` |
| `emit { event }` | Fire an event name for the engine |
| Camera actions | Section 10 |

Properties are named by their keys in the scene file, with a dot into a part:
`transform.pos`, `transform.rot`, `size`, `tint`, `view.zoom`, or a prop under
`props`.

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
for example `#camera.{transform.pos, view.zoom, view.follow, shake_offset}` and
`#hero.transform.pos`.
The engine resolves conflicts between concurrent modifiers from that, by
last-write-wins or by its own system priorities. The editor uses the same data to
warn when two modifiers that can run together write the same property.

## 10. Camera

A camera is an ordinary entity with a `view`, targeted by ordinary selectors and
actions: it shows the scene through its rect. Each new scene starts with `#camera`
tagged `.main`. A scene may have more cameras (split-screen, a minimap); the editor
flags a scene with no `.main` camera, or with several. The `.main` camera's rect is
what the screen shows, fitted to it as the design resolution is (section 11), and
the Problems panel notes a main camera whose rect has another aspect ratio; where
the others show is the engine's business.

| Property | Meaning |
|---|---|
| `transform.pos` | Where the view's origin is; a new camera's pivot is its rect's center, so this is the center of the view |
| `transform.rot` | Degrees, clockwise |
| `size` | What the view shows at zoom 1; a new camera's is the design resolution |
| `view.zoom` | 1 shows the whole rect; 2 shows half of it, twice as large |
| `view.bounds` | A selector; the union of the matched regions constrains the view |
| `view.follow` | Optional `{ target, offset, deadzone }` |
| `view.zoom_min`, `view.zoom_max` | Optional hints; the engine sets the real limits |
| `view.damping` | Optional hint |

### Camera actions

Generic actions (`move_to`, `move_along`, `tween`) work on cameras too. These only
work on cameras:

| Action | End state |
|---|---|
| `pan_to { to, duration_ms, easing }` | The target at the center of the view's rect |
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

Set in the manifest; a scene, or an entity that sets `space`, may override it.

| `fit` | Behavior |
|---|---|
| `expand` (default) | Scale uniformly to fit the smaller axis; the canvas grows on the other axis (1920×1080 becomes 2520×1080 at 21:9) |
| `letterbox` | Scale uniformly and add bars |
| `crop` | Scale uniformly to cover; the edges get cut off |
| `stretch` | Scale each axis separately |

- `integer_scale: true` is a hint for pixel art: the engine snaps the scale to a
  whole number. The "Pixel Art" project template defaults to `letterbox` with
  `integer_scale: true`.
- World space follows the same fit: with `expand`, a wider screen shows more of the
  world at zoom 1.

### Element layout

An entity with a `layout` has a rect inside its parent's: the parent's `size`, or
its own layout. In screen space, an entity whose parent has neither lays out
against the screen: the viewport, or the safe area. In world space a layout needs a
parent with a rect, and the export writes the position and size it resolves to as
authored, keeping the layout too, as for units. A `layout` takes the place of
`pos`, `at` and `size`: when an entity has both, the layout wins, and the
Problems panel warns.

```jsonc
"layout": {
  "anchor_min": [0, 0], "anchor_max": [0, 0],     // fractions of the parent rect
  "offset_min": [32, 32], "offset_max": [432, 80] } // pixels from those anchor points
```

- **Equal anchors pin** the element to a point at a fixed size. **Different anchors
  stretch** it on that axis.
- Each corner resolves as `parent.min + anchor * parent.size + offset`.
- Rotation and scale are applied around the entity's `pivot`, like a CSS
  `transform`, and never move siblings.
- `area: "safe"` (on the entity, as for percentages) lays out against the safe area;
  the engine supplies the actual insets (notches, overscan).
- An image follows its rect; a layout sets the rect, so it replaces the image's
  automatic size.
- **The inspector speaks CSS.** An anchor preset grid (3×3 points, plus stretch
  rows, columns and full) switches between `left/top/width/height` and
  `left/right/top/bottom` fields, converted to and from min/max offsets. A point
  of the grid writes `at` in percent, `pos` and `size`; a stretch writes a
  `layout`.

### Not in v1

- **Stack containers.** A `stack` (direction, spacing, padding, alignment) may come
  after v1, as another `container` type beside `tiled` (section 7). It will be
  flattened at export into plain anchors and offsets, so engines never implement
  flexbox.
- **Screen-space UI attached to world entities** (floating health bars, name
  tags). World to screen projection depends on the engine's camera, so it stays with
  the engine. A layout in world space, such as a sign on a wall, needs none.

## 12. Presets and project templates

- **Presets** are editor-only and copy-on-create: a preset bundles a shape or
  subtree, default classes, default props and an editor swatch. Creating from a
  preset copies plain entities with fresh ids; nothing stays linked, so the export
  never sees presets. Shoal ships Trigger Region, Camera Bounds, Spawn Mark and
  Camera, and projects add their own in `presets/`.
- **Linked prefabs are out of scope**: they need tree diffing and cross-file
  reference tracking, which conflicts with simple export structures.
- **Project templates** set manifest defaults and presets for a new project:
  "Default" (1920×1080, `expand`) and "Pixel Art" (640×360, `letterbox`,
  `integer_scale`).
- **A new project** has one scene, `main`, as its start scene, and a
  `classes.json` declaring the classes the built-in presets use (`camera_bounds`,
  `main`, `spawn`, `trigger`).
- **A new scene** is the size of the design resolution, with two top-level
  entities, `#world` and `#hud` (`space: "screen"`), and `#camera` tagged `.main` in
  `#world`, at the scene's center.
- **Preset files** start with `"format": "shoal-preset"` and a `version`, like
  every source file:

  ```jsonc
  // presets/enemy_grunt.json
  { "format": "shoal-preset", "version": 2,
    "name": "Enemy Grunt",              // shown in the editor; the file name if absent
    "doc": "A basic walker",            // a tooltip
    "swatch": "#c04040",                // the editor's color for it
    "classes": ["enemy"],               // given to each top-level entity it creates
    "entities": [                       // one or more, as in a scene
      { "image": { "sheet": "hero", "sprite": "hero/idle.png" },
        "props": { "hp": 3 } } ] }
  ```

  - Positions are relative to where the preset is placed. The entities go into the
    current parent; ids are made unique in the scene (`camera`, then `camera_2`),
    and entities without one stay without one.
  - A preset file is kept as written, and read only when it is used: one the
    editor can't read is listed with the reason and can't be placed, but never
    stops the project from opening. A version 1 preset is read as its entities
    were in scene format 1, migrated as a scene's are, and its `tint` as its
    `swatch`.
  - The built-in presets ship with the editor, not in the project: Trigger Region
    (`#trigger`, `.trigger`, a 128×128 rect), Camera Bounds (`.camera_bounds`, a
    1920×1080 rect), Spawn Mark (`#spawn`, `.spawn`) and Camera (`#camera`,
    `.main`, a view of the design resolution, its pivot at the center). A project
    preset with the same id replaces one.
  - **Presets are edited as scenes are**, in the Presets workspace (section 13):
    their entities are a tree with no rect, placed at a point, and each edit to
    them writes the preset's `entities` back, one undo step. A built-in preset is
    read-only until it is customized, which copies it into the project under the
    same id; reverting removes the copy.

## 13. The editor

- **Flow view (macro):** scenes as nodes, transitions as edges, modifiers as
  smaller nodes attached to their host scene. Double-click a scene to open it, an
  edge to edit the transition, a modifier to edit its action list.
  - The start scene has a colored strip. `push` edges are dashed, since the scene
    underneath stays. Edges between the same two scenes bend apart; an edge from a
    scene to itself loops above it.
  - Transitions that lead to no scene (`pop`, `push_modifier`, a missing scene)
    are listed on the node they leave from.
  - Dragging from a node's port to another node adds a `replace` transition with
    no trigger yet, named `<from>_to_<to>`. The context menu adds one to any
    scene, including the same one.
  - **A drag is one undo step**, however many nodes it moves, pushed when the
    mouse is released; a click that moves nothing changes nothing. The document is
    then modified, and nothing is written until the user saves: there is no
    autosave to the project itself.
  - A scene with no stored position is placed below the others, without changing
    the project, until the user moves it. Opening a project never modifies it.
  - The scenes panel lists every scene, the start scene marked, and shares the
    flow's selection; activating one opens it. The scene panel edits the one
    selected scene: its id (renaming it as the context menu does), whether the game
    starts there, its size and its fit (the project's, or its own), each one undo
    step.
- **Three workspaces**, as tabs: the flow, the presets, then each open scene. The
  flow's and the presets' can't be closed.
- **Side panels, under the tabs:** the tab bar runs across the window, and under it
  the current workspace sits between two panels, shared by every tab.
  - On the left, the **project panel**: the project's own data, a tab for each list
    the workspace works with. The flow has the scenes; a scene and the presets have
    the presets and the sheets, to place from. Coming to the presets shows the
    presets, the list that tab edits; a scene shows the list last chosen on a scene,
    the sheets at first.
  - On the right, the **context panel**: what is selected in the current tab. On the
    flow, the selected scene's settings; on a scene or the presets, the entity tree
    over the inspector.
  - They start narrow (240 px on the left, 300 px on the right) and the workspace
    takes the rest; they keep the widths the user gives them, from one project to
    the next. The View menu hides or shows each one.
- **Files:** a new project starts from a template, in memory; its folder is asked
  on the first save. Opening takes a project folder or its `project.shoal.json`,
  also from the command line. Closing or replacing a modified project asks first.
- **Presets workspace:** the preset current in the project panel's Presets list
  (every preset the project can create entities from, the built-in ones too), its
  entities on a canvas, around a cross where it is placed. The scene tree, the
  inspector and the sheets work on it as on a scene; presets are dragged from the
  list onto it to nest them, while choosing one there edits it rather than placing
  it. The tree's top level is the preset, and the
  inspector's top level its settings: its id (its file's name; a customized one
  keeps the built-in's), name, classes, color and description, each one undo step.
  - A built-in preset's entities can't be picked or changed; its settings show
    Customize. A customized one shows Revert to Built-in.
  - Under the list, New adds a preset creating one mark and Duplicate copies the
    current one, built-in or not, as a project preset, each shown in the Presets
    tab; Remove removes a project preset, or reverts a
    customized one. Renaming refuses an id another preset has, built-in ones
    included.
  - A preset creates one entity at least: removing all of its top-level entities is
    refused.
  - **Make Preset**, in the context menus of a scene's canvas and tree, makes a
    preset of the selected entities and their descendants, as drawn, in pixels,
    around the center of what they draw, and shows it. Its id is the first
    entity's, made unique, or `preset`.
- **Scene view (micro):** the canvas, with the scene tree, an inspector,
  a sprat sheet browser (drag sprites onto the canvas), selection and
  move/rotate/scale gizmos, and snapping.
  - Each open scene has a tab next to the flow's; double-clicking a scene node
    opens it. Tabs follow renames and close when their scene is removed.
  - Entities draw relative to their parent. **Flips mirror only the entity's own
    drawing**, as in sprat, not its children.
  - What the game doesn't draw (a rect with no fill, a view, a shape, a path, a
    mark) is drawn as an outline or a cross, and sits under what the game draws,
    whatever its order in the file; a room-sized region would otherwise catch every
    click. A group is drawn only as its children.
  - The scene tree lists the entities back to front, as a tree under a row for the
    top level, named after the scene, each entity with check boxes that hide and
    lock it in the editor. The top level is selected when no entity is, and choosing
    it clears the selection; it can't be dragged, hidden or locked, and dropping an
    entity on it puts it at the top level, in front. Dropping an entity on another makes
    it a child; above or below, a sibling; on the empty space after the rows, a
    top-level entity in front of the others. A drop is one undo step. Its + button
    adds a group (an entity with nothing yet) into the current parent, at the
    parent's origin; − removes the selection.
  - The Arrange menu, and the context menus of the canvas and the scene tree, bring
    the selection to the front (Ctrl+Shift+]) or forward (Ctrl+]), or send it
    backward (Ctrl+[) or to the back (Ctrl+Shift+[), among its siblings. Several
    selected entities move as a block, keeping their order. Each is one undo step,
    and the selection stays selected.
  - Hiding and locking entities (with their descendants), the current parent and
    the grid are editor state: they never modify the project. An entity is known by
    its id, so the state follows it when it moves, or by its path if it has none.
    They are kept in memory for now and will move to `.shoal/session.json`.
  - The current parent is where new entities go: the top level until the scene
    tree's context menu picks another (New Entities Go Here), and again once it is
    removed. Its row is in bold. A hidden or locked one, or one in screen space,
    takes nothing.
  - A drag is one undo step. It starts once the mouse moves past the platform's drag
    distance, so a click never moves an entity, even when the view scrolls under
    the pressed button. Snapping rounds to a 16-unit grid, which isn't drawn; Alt
    drags freely. Arrow keys nudge by a unit, or a grid cell when snapping, ten
    with Shift.
  - A tiled container's grid is drawn, dotted, above every entity, when it helps:
    when the container, or one of its children, is selected, or it is the current
    parent. On an axis in tiles, its children go from tile to tile when dragged,
    whether snapping is on or not, keeping their offset; Alt drags freely, and the
    drop picks the tile nearest to where the child would be with its old offset,
    the rest kept in its offset. Arrow keys move them a tile, ten with Shift,
    staying in the grid. Sprites and presets dropped in one go on the tile under
    the cursor, in tiles on both axes. On an axis in percent, a drag or a nudge
    changes the offset.
  - A single selected entity has a rotate handle above its outline and a scale
    handle at its bottom-right corner, the same size on screen at any zoom. They
    turn and scale it about its origin, where its pivot sits (a sprite's sprat
    pivot, unless set); Shift snaps rotation to 15 degrees. Releasing the mouse is
    one undo step, and a click changes nothing. On an entity with a size of its own
    the scale handle changes the size instead, in whole units, so a nine-slice's
    borders keep their size: the corner follows the mouse on each axis, and a side
    in percent or in tiles stays in its unit.
  - With nothing selected the inspector shows the top level, the scene itself: the
    rect its top-level entities measure against, and its settings as the flow's
    Scene panel edits them (id, start scene, size and fit). Resizing it moves and
    resizes what is in percent of it.
  - The inspector edits the selected entity's id, classes, active and visible
    flags, position, angle (degrees), scale and flips, then its parts: its size
    and pivot, its image (its mode, or removing it), its fill, its tint and tint mode,
    whether it clips, its container, its space and its parallax. A check box adds or removes each part that can be absent. Each change
    is one undo step. The fields are in sections, under the id, classes and flags:
    Transform (position, offset, angle, scale, flips), Rect (size, pivot), Look
    (image, fill, tint), Children (clip, container, tile pivot) and Camera (space,
    parallax). A position's or a size's axes are a line each, with their units, so
    the inspector fits a narrow panel, and it scrolls when the panel is shorter than it; a scale can't be zero. An entity with a size of its own shows
    its scale only when it isn't 1.
  - Size and pivot each have an "Own" check box, off for an entity placed from a
    sprite: while it is off, the fields show the sprite's values, read-only, and the
    file leaves them out. Turning it on keeps the values shown, so nothing moves;
    so does removing an image, which leaves its size and pivot set. For an entity
    without an image, the size's check box gives it a rect, as a fill does (64×64).
  - Each axis of the position and of the size has a unit box: px, %, or □ in a
    tiled container. Every unit stays listed, so the panel keeps its width; the
    ones the parent doesn't offer are greyed out. Choosing a unit keeps the entity
    where it is drawn, at its size (see editing scenes above); typing a value in
    percent or in tiles moves it. An axis that isn't in pixels shows its offset,
    in pixels, on an Offset row.
  - The pivot is a 3×3 grid of points (top-left, top, top-right, ...,
    bottom-right), with fields for other fractions. Changing it keeps the rect
    where it is drawn. Tint and fill are colors with alpha.
  - The container is Free or Tiled. Tiled shows the tile size and the tile pivot,
    a 3×3 grid of points like the pivot's; a new tiled container has 32×32 tiles
    centered on their pivots, and an entity without a rect is given one (64×64).
  - An id must be valid and unique in the scene; renaming it renames it in the
    transitions, and one a transition names can't be removed. Classes are typed
    separated by spaces or commas, with an optional leading dot; they complete from
    the class registry and show their declarations' docs as a tooltip.
  - Class declarations (`classes.json`) have no panel for now: they are edited by
    hand, and the inspector completes classes from them and shows their docs.
  - The presets panel lists the built-in and project presets; dropping one on the
    canvas, or double-clicking it, places it as a sprite is placed, in one undo
    step.
  - **The presets and the sheets tell a click from a drag.** A row becomes current
    when the mouse is released on it, not when it is pressed, so dragging a row away
    leaves the current one as it was: dragging a sprite onto a scene doesn't change
    the selected entity's image, and dragging a preset into the one being edited
    doesn't change which one that is. A double-click makes the row current too.
  - The sheet browser lists each declared sheet with its sprites (by path, in the
    sheet's order) and animations, as thumbnails, and filters them by name. A sheet
    that didn't load says why. It follows the sheet cache, so a sheet written again
    by sprat shows up at once.
  - **It picks the selected entity's image.** With one entity selected that can be
    changed, its sprite or animation is the current row, or None is checked;
    clicking another row, or None, sets it, one undo step. A nine-slice stays one
    with another sprite, and a size of its own stays; None keeps the size and pivot
    the image gave it. Dragging a row still places a new entity, and leaves the
    picked image as it was; double-clicking one places it only when no entity is
    picked for.
  - Dropping `.sprat` files on the sheet browser declares them in the manifest, in
    one undo step, each under an alias made from its file name (characters an id
    can't have become `_`, and `_2`, `_3`, ... avoids a clash). The path is relative
    to the project folder, or absolute while the project has never been saved. A
    file already declared is skipped. With no sheets, the browser says to drop some.
  - Dropping a sprite or an animation on the canvas adds an entity with that image
    and no id, in the current parent, at the drop point (snapped when
    snapping is on), and selects it; double-clicking it in the browser places it in
    the middle of the view. Either is one undo step. A hidden or locked current
    parent refuses it, since the new entity couldn't be seen or picked. A sprite
    with nine-slice insets is placed in `nineslice` mode with its natural size set,
    so the scale handle resizes it.
  - Entities are drawn with their effective tint.
  - Screen-space entities are listed but not drawn until the UI layout step.
- **Transition editor:** trigger and requirements, the effect and its parameters,
  the persisted state, and the mapping of entities to marks.
- **Modifier editor:** the host scene's canvas with the action list underneath.
  Ghosts show start and end positions; paths are drawn on the canvas.
- **Rehearsal:** a play mode that steps through the flow and scrubs modifiers with
  linear tweens and sprat animations played in place. It is approximate on
  purpose; matching the engine's physics or timing is out of scope.
- **Look through camera:** shows the viewport as the player sees it, with parallax,
  screen space and clamped bounds.
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
  - `space`, `parallax` or `fit` set below a moved parent, a layout, or another
    entity that sets them (an error: the export refuses the scene); a part that
    needs a rect on an entity without one; a world-space layout, or a position or
    a size in percent, whose parent has no rect.
  - A position or a size in tiles outside a tiled container, or a tile outside the
    grid.

### Sprat integration

A `SheetCache` follows sprat-lib's "load, bind, convert, free" pattern: it loads
each `.sprat` with `sprat_sheet_load_file`, copies sprites and animations into Qt
structures in the sheet's order, loads each atlas image relative to the sheet's
folder, and frees the sheet. The canvas draws sprites with sprat-lib's drawing
recipe (trim, pivot, packing rotation, flips).

- **Hot reload.** A `QFileSystemWatcher` reloads a sheet when sprat-gui or
  sprat-cli writes it or one of its atlases again, after a short pause so a burst of
  writes reloads once. A sheet declared before it exists loads when it appears.
- **A failed reload keeps the last good sheet**, such as one that caught a file half
  written, and the error is shown until a later write loads.
- **World units are the art's original pixels:** sprites are drawn at
  `1 / sheet.scale`, so packing a sheet at half scale doesn't halve the scene.
- An atlas that can't be read, or whose size differs from what the sheet says, is
  a warning; its sprites draw as placeholders.

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
- **What the export resolves:**
  - Ids from paths are written explicitly, and sheet aliases become a sheet table
    that entities index into.
  - An image's automatic size and pivot are written out (marked automatic for an
    animation).
  - Positions and sizes in percent or in tiles, and laid-out entities, carry the
    pixels they resolve to as authored, with their units or `layout`; screen
    elements also carry a precomputed `design_rect`. Children of tiled containers
    carry the position their tiles resolve to, with the tiles, and the container
    its grid's columns and rows.
  - Tints are four bytes (RGBA), with their mode.
  - Each clip says whether it stays axis-aligned (no rotation in it or above it),
    so an engine can use a scissor rectangle instead of a stencil.
  - Selectors are compiled term lists, and each modifier carries its write set.
  - Editor data, presets and class docs are dropped.
- **shoal-raw** follows sprat-raw: a version on the first line (`shoal-raw 1`), one
  record per line (`flow`, `state`, `sheet`, `scene`, `entity`,
  `transition`, `selector`, `modifier`, `action`), length-prefixed strings, totals
  declared before the records they count so a loader allocates one block, parents
  and references as indexes. A camera is an `entity` record and its actions are
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
│       ├── scene/     canvas, scene tree, inspector, sheet browser
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
4. **Scene view, world space:** the entity tree, the sprat sheet browser, sprite
   drawing and hot reload, gizmos, ids and classes, the class registry, presets;
   then entities with parts, scene format 3 and the migrations from formats 1 and
   2, tiled containers, and units (px, %, □) for positions and sizes.
5. **Spatial primitives:** marks, paths and regions; placeholders and ghosts; the
   Problems panel.
6. **Transitions:** triggers, effects, persistence, push/pop and overlays.
7. **Screen space:** anchors and offsets, the CSS-style inspector, fit modes, the
   viewport switcher.
8. **Modifiers and camera:** action lists, camera entities and actions, write sets,
   rehearsal, look through camera.
9. **Export:** the export model, JSON and raw writers, the format specification.

After v1: shoal-lib, a reference demo with sprat-raylib, stack containers, keyframe
tracks, a command-line exporter, a WASM build, zipped projects.

## 18. Open questions

- **Typed props:** a number written as `4.0` reads back as `4`, so JSON alone can't
  keep an `int` prop apart from a `float` one. Props are kept as written until this
  is decided: declare prop types somewhere (per class, or per prop as
  `{ "float": 4 }`), or write float props always with a decimal point.
- **Text:** literal strings, localization keys, or both? How are fonts referenced?
- **Audio:** stage-direction cues (`cue("door_creak")`) that the engine maps to
  files, or no audio in v1?
- **References across scenes:** can an id point into another scene
  (`level_2#door_west`), or does everything cross scenes through transitions only?
- **Events and requirements registry:** should `flow.json` declare event and
  requirement names with docs, as `classes.json` does for classes, so typos can be
  caught?
