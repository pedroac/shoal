#include "core/SceneTree.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <utility>

namespace shoal::core {

namespace {

/// A 2D affine transform, mapping a point as a row vector like QTransform (which is in
/// QtGui): x' = x * m11 + y * m21 + dx, y' = x * m12 + y * m22 + dy.
struct Affine {
    double m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;

    /// `*this`, then `next`.
    Affine then(const Affine& next) const
    {
        return {m11 * next.m11 + m12 * next.m21,         m11 * next.m12 + m12 * next.m22,
                m21 * next.m11 + m22 * next.m21,         m21 * next.m12 + m22 * next.m22,
                dx * next.m11 + dy * next.m21 + next.dx, dx * next.m12 + dy * next.m22 + next.dy};
    }

    double determinant() const { return m11 * m22 - m12 * m21; }

    Affine inverted() const
    {
        const double det = determinant();
        return {m22 / det,
                -m12 / det,
                -m21 / det,
                m11 / det,
                (m21 * dy - m22 * dx) / det,
                (m12 * dx - m11 * dy) / det};
    }
};

constexpr double DEGREES = 180 / std::numbers::pi;

/// Scale, then rotation (clockwise, Y down), then translation, as the canvas draws it.
Affine local(const Transform& t)
{
    const double c = std::cos(t.rot / DEGREES);
    const double s = std::sin(t.rot / DEGREES);
    return {t.scale.x * c, t.scale.x * s, -t.scale.y * s, t.scale.y * c, t.pos.x, t.pos.y};
}

/// `t` measured from `offset` in the parent, as an entity's alignment places it.
Affine local(const Transform& t, Point offset)
{
    Affine result = local(t);
    result.dx += offset.x;
    result.dy += offset.y;
    return result;
}

/// Where the entity at `path` is drawn in the scene's coordinates. Identity for an empty
/// path, the top level.
Affine world(const Scene& scene, const EntityPath& path, const ImageMetricsFn& metrics)
{
    Affine result;
    Frame frame = sceneFrame(scene);
    for (qsizetype depth = 1; depth <= path.size(); ++depth) {
        const Entity* entity = entityAt(scene, path.first(depth));
        result = local(entity->transform, anchorPoint(frame, entity->at)).then(result);
        frame = childFrame(*entity, frame, metrics);
    }
    return result;
}

/// Rounds away the noise of the arithmetic, which would otherwise end up in the file.
double tidy(double value)
{
    constexpr double PRECISION = 1e6;
    const double rounded = std::round(value * PRECISION) / PRECISION;
    return rounded == 0 ? 0 : rounded;
}

/// The inverse of pathAfterRemoval(): `path`, in the tree without `removed`, in the tree
/// with it.
EntityPath pathBeforeRemoval(EntityPath path, const EntityPath& removed)
{
    const qsizetype depth = removed.size() - 1;
    if (path.size() > depth && path.first(depth) == removed.first(depth) &&
        path[depth] >= removed[depth]) {
        ++path[depth];
    }
    return path;
}

template <typename SceneT, typename EntityT>
EntityT* find(SceneT& scene, const EntityPath& path)
{
    auto* list = &scene.entities;
    EntityT* entity = nullptr;
    for (int index : path) {
        if (index < 0 || size_t(index) >= list->size()) {
            return nullptr;
        }
        entity = &(*list)[size_t(index)];
        list = &entity->children;
    }
    return entity;
}

void visitAll(const std::vector<Entity>& entities, EntityPath& path,
              const std::function<void(const Entity&, const EntityPath&)>& visit)
{
    for (size_t i = 0; i < entities.size(); ++i) {
        path.append(int(i));
        visit(entities[i], path);
        visitAll(entities[i].children, path, visit);
        path.removeLast();
    }
}

}  // namespace

namespace {
const QString PRESET_TREE_PREFIX = QStringLiteral("preset:");
}  // namespace

QString presetTreeId(const QString& presetId)
{
    return PRESET_TREE_PREFIX + presetId;
}

std::optional<QString> presetOfTree(QStringView treeId)
{
    if (!treeId.startsWith(PRESET_TREE_PREFIX)) {
        return std::nullopt;
    }
    return treeId.mid(PRESET_TREE_PREFIX.size()).toString();
}

const Entity* entityAt(const Scene& scene, const EntityPath& path)
{
    return find<const Scene, const Entity>(scene, path);
}

Entity* entityAt(Scene& scene, const EntityPath& path)
{
    return find<Scene, Entity>(scene, path);
}

std::vector<Entity>* siblingsOf(Scene& scene, const EntityPath& path)
{
    if (path.size() <= 1) {
        return &scene.entities;
    }
    Entity* parent = entityAt(scene, path.first(path.size() - 1));
    return parent ? &parent->children : nullptr;
}

bool isAncestor(const EntityPath& ancestor, const EntityPath& path)
{
    return ancestor.size() < path.size() && path.first(ancestor.size()) == ancestor;
}

QString kindOf(const Entity& entity)
{
    const auto has = [&](const char* part) {
        return entity.extras.contains(QLatin1StringView(part));
    };
    if (has("view")) {
        return QStringLiteral("camera");
    }
    if (entity.image) {
        return entity.image->isStandalone()                 ? QStringLiteral("image")
               : entity.image->isAnimation()                ? QStringLiteral("animation")
               : entity.image->mode == ImageMode::NineSlice ? QStringLiteral("nine-slice")
                                                            : QStringLiteral("sprite");
    }
    if (entity.video) {
        return QStringLiteral("video");
    }
    if (entity.text) {
        return QStringLiteral("text");
    }
    if (has("path")) {
        return QStringLiteral("path");
    }
    if (has("shape")) {
        return QStringLiteral("shape");
    }
    if (entity.audio && !entity.fill) {
        return QStringLiteral("sound");
    }
    if (entity.size) {
        return entity.fill ? QStringLiteral("rect") : QStringLiteral("region");
    }
    return entity.children.empty() ? QStringLiteral("mark") : QStringLiteral("group");
}

QString describe(const Entity& entity)
{
    return entity.id.isEmpty() ? kindOf(entity) : entity.id;
}

namespace {

double& on(Point& point, int axis)
{
    return axis == 0 ? point.x : point.y;
}

double on(const Point& point, int axis)
{
    return axis == 0 ? point.x : point.y;
}

double on(const SizeF& size, int axis)
{
    return axis == 0 ? size.width : size.height;
}

double rectMin(const RectF& rect, int axis)
{
    return axis == 0 ? rect.x : rect.y;
}

double rectSide(const RectF& rect, int axis)
{
    return axis == 0 ? rect.width : rect.height;
}

/// Where `entity`'s origin is, in its parent's coordinates.
Point originIn(const Frame& frame, const Entity& entity)
{
    const Point anchor = anchorPoint(frame, entity.at);
    return {anchor.x + entity.transform.pos.x, anchor.y + entity.transform.pos.y};
}

}  // namespace

bool Frame::offers(Unit unit) const
{
    switch (unit) {
        case Unit::Px:
            return true;
        case Unit::Percent:
            return rect.has_value();
        case Unit::Cell:
            return grid.has_value();
    }
    return false;
}

Frame sceneFrame(const Scene& scene)
{
    return {RectF{0, 0, double(scene.size.width), double(scene.size.height)}, std::nullopt};
}

double resolved(const Length& length, int axis, const Frame& frame)
{
    switch (length.unit) {
        case Unit::Px:
            return length.value;
        case Unit::Percent:
            return frame.rect ? length.value / 100 * rectSide(*frame.rect, axis) : 0;
        case Unit::Cell:
            return frame.grid ? frame.grid->span(axis, length.value) : 0;
    }
    return 0;
}

std::optional<Length> lengthIn(Unit unit, double pixels, int axis, const Frame& frame)
{
    if (!frame.offers(unit)) {
        return std::nullopt;
    }
    const double per = resolved(Length{1, unit}, axis, frame);
    if (per == 0) {
        return std::nullopt;
    }
    return Length{tidy(pixels / per), unit};
}

Extent extentIn(const Extent& units, SizeF size, const Frame& frame)
{
    const auto side = [&](const Length& like, int axis) {
        return lengthIn(like.unit, on(size, axis), axis, frame)
            .value_or(Length::px(tidy(on(size, axis))));
    };
    return {side(units.width, 0), side(units.height, 1)};
}

std::optional<SizeF> entitySize(const Entity& entity, const Frame& frame,
                                const ImageMetricsFn& metrics)
{
    if (entity.size) {
        return SizeF{std::max(0.0, resolved(entity.size->width, 0, frame)),
                     std::max(0.0, resolved(entity.size->height, 1, frame))};
    }
    if (entity.image && metrics) {
        if (const std::optional<ImageMetrics> image = metrics(*entity.image)) {
            return image->size;
        }
    }
    return std::nullopt;
}

Point entityPivot(const Entity& entity, const ImageMetricsFn& metrics)
{
    if (entity.pivot) {
        return *entity.pivot;
    }
    if (entity.image && metrics) {
        if (const std::optional<ImageMetrics> image = metrics(*entity.image)) {
            return image->pivot;
        }
    }
    return {};
}

std::optional<RectF> entityRect(const Entity& entity, const Frame& frame,
                                const ImageMetricsFn& metrics)
{
    const std::optional<SizeF> size = entitySize(entity, frame, metrics);
    if (!size) {
        return std::nullopt;
    }
    const Point pivot = entityPivot(entity, metrics);
    return RectF{-pivot.x * size->width, -pivot.y * size->height, size->width, size->height};
}

Point TileGrid::point(Cell cell) const
{
    return {pointOn(0, cell.column), pointOn(1, cell.row)};
}

double TileGrid::pointOn(int axis, double index) const
{
    return on(origin, axis) + (index + on(pivot, axis)) * on(tile, axis);
}

int TileGrid::nearest(int axis, double at) const
{
    const double index = std::round((at - on(origin, axis)) / on(tile, axis) - on(pivot, axis));
    const int count = axis == 0 ? columns : rows;
    return int(std::clamp(index, 0.0, double(std::max(count - 1, 0))));
}

double TileGrid::span(int axis, double count) const
{
    return count * on(tile, axis);
}

std::optional<TileGrid> tileGrid(const Entity& container, const Frame& frame,
                                 const ImageMetricsFn& metrics)
{
    if (!container.container || container.container->type != ContainerType::Tiled) {
        return std::nullopt;
    }
    const std::optional<RectF> rect = entityRect(container, frame, metrics);
    if (!rect) {
        return std::nullopt;
    }
    const Container& tiled = *container.container;
    const auto fitting = [](double side, double tileSide) {
        // A tile that fits but for the arithmetic's noise still counts.
        constexpr double TOLERANCE = 1e-9;
        return int(std::min(std::floor(side / tileSide + TOLERANCE), double(1 << 24)));
    };
    return TileGrid{{rect->x, rect->y},
                    tiled.tileSize,
                    tiled.tilePivot,
                    fitting(rect->width, tiled.tileSize.width),
                    fitting(rect->height, tiled.tileSize.height)};
}

Frame childFrame(const Entity& entity, const Frame& frame, const ImageMetricsFn& metrics)
{
    return {entityRect(entity, frame, metrics), tileGrid(entity, frame, metrics)};
}

Frame frameIn(const Scene& scene, const EntityPath& parent, const ImageMetricsFn& metrics)
{
    Frame frame = sceneFrame(scene);
    for (qsizetype depth = 1; depth <= parent.size(); ++depth) {
        const Entity* entity = entityAt(scene, parent.first(depth));
        if (!entity) {
            return {};
        }
        frame = childFrame(*entity, frame, metrics);
    }
    return frame;
}

Point anchorPoint(const Frame& frame, const Anchor& at)
{
    Point point;
    for (int axis = 0; axis < 2; ++axis) {
        const Length& length = at.axis(axis);
        if (length.unit == Unit::Percent && frame.rect) {
            on(point, axis) =
                rectMin(*frame.rect, axis) + length.value / 100 * rectSide(*frame.rect, axis);
        }
        else if (length.unit == Unit::Cell && frame.grid) {
            on(point, axis) = frame.grid->pointOn(axis, length.value);
        }
    }
    return point;
}

Entity reanchored(const Frame& frame, Entity entity, int axis, Unit unit)
{
    if (!frame.offers(unit)) {
        return entity;
    }
    const double origin = on(originIn(frame, entity), axis);
    Length& at = entity.at.axis(axis);
    double& pos = on(entity.transform.pos, axis);
    switch (unit) {
        case Unit::Px:
            at = {};
            pos = tidy(origin);
            break;
        case Unit::Percent: {
            const double side = rectSide(*frame.rect, axis);
            const double from = origin - rectMin(*frame.rect, axis);
            at = {side > 0 ? tidy(from / side * 100) : 0, Unit::Percent};
            pos = side > 0 ? 0 : tidy(from);
            break;
        }
        case Unit::Cell: {
            const int index = frame.grid->nearest(axis, origin);
            at = {double(index), Unit::Cell};
            pos = tidy(origin - frame.grid->pointOn(axis, index));
            break;
        }
    }
    return entity;
}

Entity movedTo(const Frame& frame, Entity entity, Point origin)
{
    for (int axis = 0; axis < 2; ++axis) {
        Length& at = entity.at.axis(axis);
        double& pos = on(entity.transform.pos, axis);
        if (at.unit == Unit::Cell && frame.grid) {
            at.value = frame.grid->nearest(axis, on(origin, axis) - pos);
        }
        else {
            pos = tidy(on(origin, axis) - on(anchorPoint(frame, entity.at), axis));
        }
    }
    return entity;
}

Point positionForPivot(const Entity& entity, const Frame& frame, std::optional<Point> pivot,
                       const ImageMetricsFn& metrics)
{
    const std::optional<SizeF> size = entitySize(entity, frame, metrics);
    if (!size) {
        return entity.transform.pos;
    }
    Entity pivoted = entity;
    pivoted.pivot = pivot;
    const Point from = entityPivot(entity, metrics);
    const Point to = entityPivot(pivoted, metrics);
    // The origin moves by the pivot's change, through the entity's rotation and scale.
    const Affine m = local(entity.transform);
    const double x = (to.x - from.x) * size->width;
    const double y = (to.y - from.y) * size->height;
    return {tidy(m.dx + x * m.m11 + y * m.m21), tidy(m.dy + x * m.m12 + y * m.m22)};
}

Entity placedInContainer(const Frame& before, const Frame& after, Entity child,
                         const ImageMetricsFn& metrics)
{
    const Point origin = originIn(before, child);
    const std::optional<SizeF> size =
        child.size ? entitySize(child, before, metrics) : std::nullopt;
    child.transform.pos = origin;
    const Anchor from = child.at;
    child.at = {};
    for (int axis = 0; axis < 2; ++axis) {
        const Unit unit = after.grid ? Unit::Cell : from.axis(axis).unit;
        if (unit == Unit::Percent && from.axis(axis).unit == Unit::Percent && after.rect) {
            // It keeps its percentage, its offset making up the difference.
            child.at.axis(axis) = from.axis(axis);
            on(child.transform.pos, axis) =
                tidy(on(origin, axis) - on(anchorPoint(after, child.at), axis));
        }
        else {
            child = reanchored(after, std::move(child), axis, unit);
            on(child.transform.pos, axis) = tidy(on(child.transform.pos, axis));
        }
    }
    if (size) {
        child.size = extentIn(*child.size, *size, after);
    }
    return child;
}

Color effectiveTint(const Entity& entity, const Color& received)
{
    switch (entity.tintMode) {
        case TintMode::Mix:
            return received.multiplied(entity.tint);
        case TintMode::Inherit:
            return received;
        case TintMode::Override:
            return entity.tint;
    }
    return received;
}

void forEachEntity(const Scene& scene,
                   const std::function<void(const Entity&, const EntityPath&)>& visit)
{
    EntityPath path;
    visitAll(scene.entities, path, visit);
}

EntityPath findEntity(const Scene& scene, QStringView id)
{
    EntityPath found;
    if (id.isEmpty()) {
        return found;
    }
    forEachEntity(scene, [&](const Entity& entity, const EntityPath& path) {
        if (found.isEmpty() && entity.id == id) {
            found = path;
        }
    });
    return found;
}

QStringList entityIds(const Scene& scene)
{
    QStringList ids;
    forEachEntity(scene, [&](const Entity& entity, const EntityPath&) {
        if (!entity.id.isEmpty()) {
            ids << entity.id;
        }
    });
    return ids;
}

EntityPath pathAfterRemoval(EntityPath path, const EntityPath& removed)
{
    Q_ASSERT(!removed.isEmpty() && path != removed && !isAncestor(removed, path));
    const qsizetype depth = removed.size() - 1;
    if (path.size() > depth && path.first(depth) == removed.first(depth) &&
        path[depth] > removed[depth]) {
        --path[depth];
    }
    return path;
}

Transform relocatedTransform(const Scene& scene, const EntityPath& from, const EntityPath& to,
                             const ImageMetricsFn& metrics)
{
    const Entity* entity = entityAt(scene, from);
    Q_ASSERT(entity && !to.isEmpty());
    const EntityPath parent = pathBeforeRemoval(to.first(to.size() - 1), from);
    const Affine parentWorld = world(scene, parent, metrics);
    if (parent == from.first(from.size() - 1) || parentWorld.determinant() == 0) {
        return entity->transform;
    }
    const Affine m = world(scene, from, metrics).then(parentWorld.inverted());
    const Point offset = anchorPoint(frameIn(scene, parent, metrics), entity->at);

    // The scale's x keeps its sign, so a mirror stays where it was, and the angle is the
    // one nearest the old, so no turn is lost.
    Transform result = entity->transform;
    const double sign = entity->transform.scale.x < 0 ? -1 : 1;
    double rot = std::atan2(sign * m.m12, sign * m.m11) * DEGREES;
    rot += 360 * std::round((entity->transform.rot - rot) / 360);
    const double c = std::cos(rot / DEGREES);
    const double s = std::sin(rot / DEGREES);
    const auto scale = [](double value) {
        return std::abs(value) < MIN_SCALE ? std::copysign(MIN_SCALE, value) : value;
    };
    result.pos = Point{tidy(m.dx - offset.x), tidy(m.dy - offset.y)};
    result.rot = tidy(rot);
    result.scale = Point{tidy(scale(m.m11 * c + m.m12 * s)), tidy(scale(m.m22 * c - m.m21 * s))};
    return result;
}

Entity relocatedEntity(const Scene& scene, const EntityPath& from, const EntityPath& to,
                       const ImageMetricsFn& metrics)
{
    const Entity* entity = entityAt(scene, from);
    Q_ASSERT(entity && !to.isEmpty());
    Entity result = *entity;
    result.children.clear();
    const EntityPath parent = pathBeforeRemoval(to.first(to.size() - 1), from);
    if (parent == from.first(from.size() - 1)) {
        return result;
    }
    const std::optional<SizeF> size =
        entity->size
            ? entitySize(*entity, frameIn(scene, from.first(from.size() - 1), metrics), metrics)
            : std::nullopt;
    result.transform = relocatedTransform(scene, from, to, metrics);
    // Its transform is measured from its anchor as it would be in its new parent; the
    // parent's container and rect may place it otherwise.
    const Frame frame = frameIn(scene, parent, metrics);
    result = placedInContainer(frame, frame, std::move(result), metrics);
    if (size) {
        result.size = extentIn(*result.size, *size, frame);  // the size it had
    }
    return result;
}

QList<EntityPath> outermost(QList<EntityPath> paths)
{
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    QList<EntityPath> result;
    for (const EntityPath& path : paths) {
        if (result.isEmpty() || !isAncestor(result.back(), path)) {
            result << path;
        }
    }
    return result;
}

namespace {

/// Reorders `members`, sibling indices in draw order, as arranging the `arranged` ones does.
void arrangeMembers(QList<int>& members, const QSet<int>& arranged, Arrange arrange)
{
    const auto isArranged = [&](int index) { return arranged.contains(index); };
    switch (arrange) {
        case Arrange::ToFront:
            std::stable_partition(members.begin(), members.end(),
                                  [&](int index) { return !isArranged(index); });
            break;
        case Arrange::ToBack:
            std::stable_partition(members.begin(), members.end(), isArranged);
            break;
        case Arrange::Forward:
            // From the front, so a run of arranged entities moves as a block.
            for (qsizetype i = members.size() - 2; i >= 0; --i) {
                if (isArranged(members[i]) && !isArranged(members[i + 1])) {
                    std::swap(members[i], members[i + 1]);
                }
            }
            break;
        case Arrange::Backward:
            for (qsizetype i = 1; i < members.size(); ++i) {
                if (isArranged(members[i]) && !isArranged(members[i - 1])) {
                    std::swap(members[i], members[i - 1]);
                }
            }
            break;
    }
}

}  // namespace

QMap<EntityPath, QList<int>> arrangedOrders(const Scene& scene, const QList<EntityPath>& paths,
                                            Arrange arrange)
{
    QMap<EntityPath, QSet<int>> arranged;  // by parent
    for (const EntityPath& path : paths) {
        if (entityAt(scene, path)) {
            arranged[path.first(path.size() - 1)].insert(path.back());
        }
    }
    QMap<EntityPath, QList<int>> orders;
    for (auto it = arranged.cbegin(); it != arranged.cend(); ++it) {
        const EntityPath& parent = it.key();
        const std::vector<Entity>& siblings =
            parent.isEmpty() ? scene.entities : entityAt(scene, parent)->children;
        QList<int> order(qsizetype(siblings.size()));
        std::iota(order.begin(), order.end(), 0);
        const QList<int> before = order;
        arrangeMembers(order, it.value(), arrange);
        if (order != before) {
            orders.insert(parent, order);
        }
    }
    return orders;
}

EntityPath pathAfterReorder(const EntityPath& path, const QMap<EntityPath, QList<int>>& orders)
{
    EntityPath result = path;
    for (qsizetype depth = 0; depth < path.size(); ++depth) {
        const auto it = orders.constFind(path.first(depth));  // keyed by paths before
        if (it != orders.cend()) {
            result[depth] = int(it->indexOf(path[depth]));
        }
    }
    return result;
}

}  // namespace shoal::core
