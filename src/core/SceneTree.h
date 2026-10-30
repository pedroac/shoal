#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

#include "core/Model.h"

namespace shoal::core {

/// Where an entity is in its scene: the index among the top-level entities, then among
/// each parent's children. Entities need no id, so this is how the editor names them.
using EntityPath = QList<int>;

/// The id a preset's entities are edited by, as a tree of their own: the entity mutators
/// and commands take a scene's id or this. Ids have no colon, so the two never meet.
QString presetTreeId(const QString& presetId);
/// The preset `treeId` names, or null for a scene's id.
std::optional<QString> presetOfTree(QStringView treeId);

/// The entity at `path`, or null.
const Entity* entityAt(const Scene& scene, const EntityPath& path);
Entity* entityAt(Scene& scene, const EntityPath& path);

/// The list `path` indexes into: the scene's entities for a top-level path, otherwise the
/// parent's children. Null if the parent doesn't exist.
std::vector<Entity>* siblingsOf(Scene& scene, const EntityPath& path);

/// Whether `ancestor` is a strict ancestor of `path`.
bool isAncestor(const EntityPath& ancestor, const EntityPath& path);

/// What an entity is, from its parts: "camera", "sprite", "animation", "nine-slice",
/// "text", "path", "shape", "rect", "region", "group" or "mark".
QString kindOf(const Entity& entity);

/// An entity's name in a command's text: its id, or its kind.
QString describe(const Entity& entity);

// --- geometry ---

/// An image's natural size and its sprite's pivot, as a fraction of that size: what an
/// entity whose size and pivot are automatic takes. Core doesn't read sheets, so the
/// editor supplies them; null when the sprite can't be found.
struct ImageMetrics {
    SizeF size;
    Point pivot;
};
using ImageMetricsFn = std::function<std::optional<ImageMetrics>(const Image&)>;

/// A rect in an entity's own coordinates, its origin at 0, 0.
struct RectF {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    bool operator==(const RectF&) const = default;
};

/// A tiled container's grid, in its coordinates: as many whole tiles as fit in its rect,
/// from its top-left corner.
struct TileGrid {
    Point origin;  ///< The top-left corner of cell 0, 0.
    SizeF tile;
    Point pivot;  ///< The point of each tile its children's origins land on, as fractions.
    int columns = 0;
    int rows = 0;
    bool operator==(const TileGrid&) const = default;

    /// Where the origin of a child in `cell` lands, before its offset.
    Point point(Cell cell) const;
    /// The same, on one axis (0 for x, 1 for y), for a child in tile `index` on it.
    double pointOn(int axis, double index) const;
    /// The tile on `axis` whose point is nearest to `at`, kept inside the grid (0 on an
    /// axis where no tile fits).
    int nearest(int axis, double at) const;
    /// The size of `count` tiles on `axis`.
    double span(int axis, double count) const;
};

/// What a parent offers its children to measure against, in its coordinates: its rect,
/// for percentages, and its grid, for tiles. The top level measures against the scene's
/// rect.
struct Frame {
    std::optional<RectF> rect;
    std::optional<TileGrid> grid;
    bool operator==(const Frame&) const = default;

    /// Whether lengths in `unit` mean something here: pixels always, percentages with a
    /// rect, tiles with a grid.
    bool offers(Unit unit) const;
};

/// The frame of the scene's top level: its rect, from the origin to its size.
Frame sceneFrame(const Scene& scene);

/// A size's side in pixels, on `axis` (0 for width, 1 for height): a percentage of the
/// frame's rect, or a number of its tiles. 0 for a unit the frame doesn't offer.
double resolved(const Length& length, int axis, const Frame& frame);
/// `pixels` on `axis` in `unit`, measured in `frame`; null when the frame doesn't offer the
/// unit, or it can't say how much (a rect with no width, for a percentage of it).
std::optional<Length> lengthIn(Unit unit, double pixels, int axis, const Frame& frame);
/// A size of `size` pixels, in `units`' units where `frame` offers them, in pixels
/// otherwise.
Extent extentIn(const Extent& units, SizeF size, const Frame& frame);

/// The fraction of an entity's rect on its origin: its `pivot`, or its image's, or the
/// top-left corner.
Point entityPivot(const Entity& entity, const ImageMetricsFn& metrics = {});
/// The size of an entity's rect in pixels, measured in its parent's `frame`: its `size`, or
/// its image's natural size; null when it has no rect.
std::optional<SizeF> entitySize(const Entity& entity, const Frame& frame,
                                const ImageMetricsFn& metrics = {});
/// An entity's rect, placed around its origin by its pivot; null when it has none.
std::optional<RectF> entityRect(const Entity& entity, const Frame& frame,
                                const ImageMetricsFn& metrics = {});

/// `container`'s grid, measured in its parent's `frame`; null unless it is a tiled
/// container with a rect.
std::optional<TileGrid> tileGrid(const Entity& container, const Frame& frame,
                                 const ImageMetricsFn& metrics = {});
/// What `entity`, measured in its parent's `frame`, offers its children.
Frame childFrame(const Entity& entity, const Frame& frame, const ImageMetricsFn& metrics = {});
/// The frame the children of the entity at `parent` are measured in: the scene's for the
/// top level (an empty path).
Frame frameIn(const Scene& scene, const EntityPath& parent, const ImageMetricsFn& metrics = {});

/// Where a position anchored at `at` is measured from, in the parent's coordinates: on each
/// axis, the parent's origin, a percentage of its rect, or a tile's point. An axis in a
/// unit the frame doesn't offer is measured from the origin.
Point anchorPoint(const Frame& frame, const Anchor& at);
/// `entity` with its position on `axis` measured in `unit` instead, keeping it where it is
/// drawn: a percentage with no offset, the nearest tile with what is left as its offset, or
/// pixels from the parent's origin. Unchanged when the frame doesn't offer the unit.
Entity reanchored(const Frame& frame, Entity entity, int axis, Unit unit);
/// `entity` with its origin at `origin`, in its parent's coordinates: on an axis in tiles,
/// on the tile nearest to it, keeping its offset; on the others, by its offset.
Entity movedTo(const Frame& frame, Entity entity, Point origin);
/// The position that keeps `entity`'s rect where it is drawn once its pivot is `pivot`
/// instead (null for automatic). Its position, if it has no rect.
Point positionForPivot(const Entity& entity, const Frame& frame, std::optional<Point> pivot,
                       const ImageMetricsFn& metrics = {});

/// `child`, drawn where it is in the frame `before`, measured in `after` instead (the same
/// parent with another container, or its new parent once it has moved there), with the
/// same size: in a tiled container, on the tile nearest to it on each axis, the rest in
/// its offset; elsewhere in its own units where they mean something, in pixels otherwise.
Entity placedInContainer(const Frame& before, const Frame& after, Entity child,
                         const ImageMetricsFn& metrics = {});

/// The tint an entity draws with and passes to its children, from the one it receives.
Color effectiveTint(const Entity& entity, const Color& received);

/// Visits every entity, parents before children, in draw order.
void forEachEntity(const Scene& scene,
                   const std::function<void(const Entity&, const EntityPath&)>& visit);

/// The path of the entity with `id`, or empty.
EntityPath findEntity(const Scene& scene, QStringView id);

/// The ids in use in the scene.
QStringList entityIds(const Scene& scene);

/// `path` as it reads once the entity at `removed` is taken out of the tree. `path` must
/// not be inside `removed`.
EntityPath pathAfterRemoval(EntityPath path, const EntityPath& removed);

/// The transform that keeps the entity at `from` where it is drawn once it moves to `to`, a
/// path in the tree without it (see pathAfterRemoval()): relative to its new parent and
/// measured from its own anchor there. A skew that a parent's rotation and uneven scale
/// would give it can't be expressed, so it is dropped; flips are the entity's own and stay
/// as they are.
Transform relocatedTransform(const Scene& scene, const EntityPath& from, const EntityPath& to,
                             const ImageMetricsFn& metrics = {});

/// The entity at `from` as it is once it moves to `to` (see relocatedTransform()), without
/// its children: with its relocated transform, and placed as its new parent places its
/// children (see placedInContainer()). Unchanged when its parent stays the same.
Entity relocatedEntity(const Scene& scene, const EntityPath& from, const EntityPath& to,
                       const ImageMetricsFn& metrics = {});

/// `paths` without those inside another one of them, sorted.
QList<EntityPath> outermost(QList<EntityPath> paths);

/// Where arranged entities go in draw order, among their siblings.
enum class Arrange { ToFront, Forward, Backward, ToBack };

/// The new order of each list of siblings that arranging the entities at `paths` changes,
/// by parent path (empty for the top level): `order[i]` is the index, before, of the entity
/// that ends at index i. Forward and backward pass the next sibling that isn't arranged too.
QMap<EntityPath, QList<int>> arrangedOrders(const Scene& scene, const QList<EntityPath>& paths,
                                            Arrange arrange);

/// `path` once the sibling lists are reordered as `orders` says (see arrangedOrders()).
EntityPath pathAfterReorder(const EntityPath& path, const QMap<EntityPath, QList<int>>& orders);

}  // namespace shoal::core
