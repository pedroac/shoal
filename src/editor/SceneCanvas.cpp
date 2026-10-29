#include "editor/SceneCanvas.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <algorithm>
#include <cmath>
#include <memory>

#include "assets/SheetCache.h"
#include "assets/SpriteSheet.h"
#include "core/CommandStack.h"
#include "core/Presets.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/EntityItem.h"
#include "editor/TileGridOverlay.h"
#include "editor/TransformHandle.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

QString pathKey(const EntityPath& path)
{
    QStringList indexes;
    for (int index : path) {
        indexes << QString::number(index);
    }
    return indexes.join(u'.');
}

/// Whether only the editor draws an entity: see EntityItem::isGuide().
bool isGuide(const Entity& entity)
{
    return !entity.image && !entity.fill && !entity.extras.contains(QStringLiteral("text"));
}

/// `scenePosition`, snapped, in the coordinates of `parent` (the top level if empty).
QPointF positionIn(const SceneCanvas& canvas, const EntityPath& parent, QPointF scenePosition)
{
    const QPointF at = canvas.snap(scenePosition);
    const EntityItem* item = parent.isEmpty() ? nullptr : canvas.item(parent);
    return item ? item->mapFromScene(at) : at;
}

/// The shape of a scene's tree: what decides whether the items can be updated in place.
QStringList treeShape(const core::Scene& scene)
{
    QStringList shape;
    core::forEachEntity(scene, [&](const Entity& entity, const EntityPath& path) {
        // What sets z, flags and visibility too: guides go under, space and ids (for the
        // editor's hidden and locked state) decide whether it is shown and picked.
        shape << QStringLiteral("%1:%2:%3:%4")
                     .arg(pathKey(path), entity.id)
                     .arg(isGuide(entity))
                     .arg(entity.space ? int(*entity.space) : -1);
    });
    return shape;
}

}  // namespace

SceneCanvas::SceneCanvas(core::ProjectDocument& document, assets::SheetCache& sheets,
                         QString sceneId, QObject* parent)
    : QGraphicsScene(parent), m_document(document), m_sheets(sheets), m_sceneId(std::move(sceneId))
{
    connect(&m_document, &core::ProjectDocument::sceneContentChanged, this,
            [this](const QString& id) {
                if (id == m_sceneId) {
                    rebuild();
                }
            });
    // Its size moves the bounds the views draw behind the items, and the top-level
    // entities measured in percent of it.
    connect(&m_document, &core::ProjectDocument::sceneSettingsChanged, this,
            [this](const QString& id) {
                if (id == m_sceneId) {
                    rebuild();
                    update();
                }
            });
    connect(&m_document, &core::ProjectDocument::sceneRenamed, this,
            [this](const QString& from, const QString& to) {
                if (from == m_sceneId) {
                    m_sceneId = to;
                }
            });
    connect(&m_document, &core::ProjectDocument::sceneRemoved, this, [this](const QString& id) {
        if (id == m_sceneId) {
            m_sceneId.clear();
            rebuild();
        }
    });
    connect(&m_document, &core::ProjectDocument::changed, this, [this] {
        // Class colors and the design resolution live outside the scene.
        for (EntityItem* item : std::as_const(m_items)) {
            item->update();
        }
    });
    connect(&m_sheets, &assets::SheetCache::sheetChanged, this, &SceneCanvas::onSheetChanged);
    connect(this, &QGraphicsScene::selectionChanged, this, &SceneCanvas::updateHandles);
    connect(this, &SceneCanvas::currentParentChanged, this, &SceneCanvas::updateGrids);

    rebuild();
}

SceneCanvas::~SceneCanvas()
{
    // Delete the items while this is still a SceneCanvas: deleting selected items emits
    // selectionChanged(), and its receivers query this canvas.
    clearItems();
}

const core::Scene* SceneCanvas::scene() const
{
    const auto& scenes = m_document.project().scenes;
    const auto it = scenes.find(m_sceneId);
    return it == scenes.end() ? nullptr : &*it;
}

QRectF SceneCanvas::sceneBounds() const
{
    const core::Scene* current = scene();
    return current ? QRectF(0, 0, current->size.width, current->size.height) : QRectF();
}

EntityItem* SceneCanvas::item(const EntityPath& path) const
{
    return m_items.value(path);
}

QList<EntityPath> SceneCanvas::selectedPaths() const
{
    QList<EntityPath> paths;
    for (QGraphicsItem* selected : selectedItems()) {
        if (selected->type() == EntityItem::Type) {
            paths << static_cast<EntityItem*>(selected)->path();
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

void SceneCanvas::select(const QList<EntityPath>& paths)
{
    QSignalBlocker blocker(this);  // one selectionChanged() for the whole change
    clearSelection();
    for (const EntityPath& path : paths) {
        if (EntityItem* entity = item(path);
            entity && (entity->flags() & QGraphicsItem::ItemIsSelectable)) {
            entity->setSelected(true);
        }
    }
    blocker.unblock();
    emit selectionChanged();
}

void SceneCanvas::setCurrentParent(const EntityPath& path)
{
    const Entity* entity = scene() && !path.isEmpty() ? core::entityAt(*scene(), path) : nullptr;
    const QString id = entity ? entity->id : QString();
    if (m_currentParent != path || m_currentParentId != id) {
        m_currentParent = path;
        m_currentParentId = id;
        emit currentParentChanged();
    }
}

QString SceneCanvas::stateKey(const EntityPath& path) const
{
    const Entity* entity = scene() ? core::entityAt(*scene(), path) : nullptr;
    return entity && !entity->id.isEmpty() ? u'#' + entity->id : u'@' + pathKey(path);
}

bool SceneCanvas::isHidden(const EntityPath& path) const
{
    return m_hidden.contains(stateKey(path));
}

void SceneCanvas::setHidden(const EntityPath& path, bool hidden)
{
    const QString key = stateKey(path);
    if (hidden == m_hidden.contains(key)) {
        return;
    }
    if (hidden) {
        m_hidden.insert(key);
    }
    else {
        m_hidden.remove(key);
    }
    m_treeShape.clear();  // the items' flags and visibility change
    rebuild();
    emit entityStateChanged();
}

bool SceneCanvas::isLocked(const EntityPath& path) const
{
    return m_locked.contains(stateKey(path));
}

void SceneCanvas::setLocked(const EntityPath& path, bool locked)
{
    const QString key = stateKey(path);
    if (locked == m_locked.contains(key)) {
        return;
    }
    if (locked) {
        m_locked.insert(key);
    }
    else {
        m_locked.remove(key);
    }
    m_treeShape.clear();  // the items' flags and visibility change
    rebuild();
    emit entityStateChanged();
}

bool SceneCanvas::isHiddenOrLocked(const EntityPath& path) const
{
    for (qsizetype depth = 1; depth <= path.size(); ++depth) {
        if (isHidden(path.first(depth)) || isLocked(path.first(depth))) {
            return true;
        }
    }
    return false;
}

bool SceneCanvas::isInScreenSpace(const EntityPath& path) const
{
    const core::Scene* current = scene();
    bool screen = false;
    for (qsizetype depth = 1; current && depth <= path.size(); ++depth) {
        const Entity* entity = core::entityAt(*current, path.first(depth));
        if (entity && entity->space) {
            screen = *entity->space == core::Space::Screen;
        }
    }
    return screen;
}

void SceneCanvas::setSnapping(bool snapping)
{
    m_snapping = snapping;
}

void SceneCanvas::setGridSize(double size)
{
    if (size > 0) {
        m_gridSize = size;
        update();
    }
}

QPointF SceneCanvas::snap(QPointF point) const
{
    if (!m_snapping) {
        return point;
    }
    return {std::round(point.x() / m_gridSize) * m_gridSize,
            std::round(point.y() / m_gridSize) * m_gridSize};
}

// --- edits ---

core::Frame SceneCanvas::currentFrame() const
{
    return scene() ? core::frameIn(*scene(), m_currentParent, imageMetrics()) : core::Frame{};
}

bool SceneCanvas::canAddEntities() const
{
    const core::Scene* current = scene();
    return current && (m_currentParent.isEmpty() || core::entityAt(*current, m_currentParent)) &&
           !isHiddenOrLocked(m_currentParent) && !isInScreenSpace(m_currentParent);
}

EntityPath SceneCanvas::addEntity(Entity entity, QPointF scenePosition)
{
    const core::Scene* current = scene();
    if (!canAddEntities()) {
        return {};
    }
    const QPointF at = positionIn(*this, m_currentParent, scenePosition);
    entity.transform.pos = core::Point{at.x(), at.y()};
    entity.at = {};
    if (const core::Frame frame = currentFrame(); frame.grid) {
        // On the tile under the cursor.
        for (int axis = 0; axis < 2; ++axis) {
            const double along = axis == 0 ? at.x() : at.y();
            entity.at.axis(axis) = {double(frame.grid->nearest(axis, along)), core::Unit::Cell};
        }
        entity.transform.pos = {};
    }
    const std::vector<Entity>& siblings = m_currentParent.isEmpty()
                                              ? current->entities
                                              : core::entityAt(*current, m_currentParent)->children;
    EntityPath path = m_currentParent;
    path.append(int(siblings.size()));
    m_document.commands().push(
        std::make_unique<core::AddEntityCommand>(m_document, m_sceneId, path, std::move(entity)));
    select({path});
    return path;
}

QList<EntityPath> SceneCanvas::addPreset(const QString& presetId, QPointF scenePosition,
                                         QString* error)
{
    const core::Scene* current = scene();
    if (!canAddEntities()) {
        return {};
    }
    const QMap<QString, core::Preset> presets = core::availablePresets(m_document.project());
    if (!presets.contains(presetId)) {
        return {};
    }
    const core::PresetRead read = core::readPresetDefinition(presetId, presets[presetId]);
    if (!read.definition) {
        if (error) {
            *error = read.error;
        }
        return {};
    }
    const QPointF at = positionIn(*this, m_currentParent, scenePosition);
    const core::Frame frame = currentFrame();
    // In a tiled container, the preset's entities go on the tile under the cursor, placed
    // from it as they are from the preset's origin.
    std::vector<Entity> entities = core::instantiatePreset(
        *read.definition, *current, frame.grid ? core::Point{} : core::Point{at.x(), at.y()});
    if (frame.grid) {
        const core::Anchor tile{{double(frame.grid->nearest(0, at.x())), core::Unit::Cell},
                                {double(frame.grid->nearest(1, at.y())), core::Unit::Cell}};
        for (Entity& entity : entities) {
            entity.at = tile;
        }
    }
    QList<EntityPath> paths;
    int index =
        int(m_currentParent.isEmpty() ? current->entities.size()
                                      : core::entityAt(*current, m_currentParent)->children.size());
    core::CommandStack& commands = m_document.commands();
    commands.beginMacro(tr("Add %1").arg(read.definition->name));
    for (Entity& entity : entities) {
        EntityPath path = m_currentParent;
        path.append(index++);
        paths << path;
        commands.push(std::make_unique<core::AddEntityCommand>(m_document, m_sceneId, paths.last(),
                                                               std::move(entity)));
    }
    commands.endMacro();
    select(paths);
    return paths;
}

void SceneCanvas::removeSelection()
{
    const QList<EntityPath> paths = selectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    clearSelection();  // the paths will name other entities afterwards
    m_document.commands().push(
        std::make_unique<core::RemoveEntitiesCommand>(m_document, m_sceneId, paths));
}

void SceneCanvas::arrangeSelection(core::Arrange arrange)
{
    const core::Scene* current = scene();
    if (!current) {
        return;
    }
    const QList<EntityPath> paths = selectedPaths();
    const QMap<EntityPath, QList<int>> orders = core::arrangedOrders(*current, paths, arrange);
    if (orders.isEmpty()) {
        return;  // already there
    }
    const QString text = arrangeText(arrange);
    // Deepest lists first: reordering a list changes the paths below it, not above.
    QList<EntityPath> parents = orders.keys();
    std::stable_sort(parents.begin(), parents.end(),
                     [](const EntityPath& a, const EntityPath& b) { return a.size() > b.size(); });
    QList<EntityPath> after;
    for (const EntityPath& path : paths) {
        after << core::pathAfterReorder(path, orders);
    }
    clearSelection();  // the paths will name other entities meanwhile
    core::CommandStack& commands = m_document.commands();
    commands.beginMacro(text);
    for (const EntityPath& parent : parents) {
        commands.push(std::make_unique<core::ReorderEntitiesCommand>(m_document, m_sceneId, parent,
                                                                     orders.value(parent), text));
    }
    commands.endMacro();
    select(after);
}

QString SceneCanvas::arrangeText(core::Arrange arrange)
{
    switch (arrange) {
        case core::Arrange::ToFront:
            return tr("Bring to Front");
        case core::Arrange::Forward:
            return tr("Bring Forward");
        case core::Arrange::Backward:
            return tr("Send Backward");
        case core::Arrange::ToBack:
            return tr("Send to Back");
    }
    return {};
}

QKeySequence SceneCanvas::arrangeShortcut(core::Arrange arrange)
{
    switch (arrange) {
        case core::Arrange::ToFront:
            return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight);
        case core::Arrange::Forward:
            return QKeySequence(Qt::CTRL | Qt::Key_BracketRight);
        case core::Arrange::Backward:
            return QKeySequence(Qt::CTRL | Qt::Key_BracketLeft);
        case core::Arrange::ToBack:
            return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft);
    }
    return {};
}

void SceneCanvas::addArrangeActions(QMenu& menu)
{
    const QList<EntityPath> paths = selectedPaths();
    const core::Scene* current = scene();
    for (const core::Arrange arrange : ARRANGES) {
        QAction* action = menu.addAction(arrangeText(arrange), this,
                                         [this, arrange] { arrangeSelection(arrange); });
        action->setShortcut(arrangeShortcut(arrange));  // shown; the window's action has it
        action->setEnabled(current && !core::arrangedOrders(*current, paths, arrange).isEmpty());
    }
}

void SceneCanvas::nudgeSelection(QPoint direction, bool large)
{
    const int count = large ? NUDGE_MULTIPLIER : 1;
    const double step = (m_snapping ? m_gridSize : 1) * count;
    QList<std::pair<EntityPath, Entity>> changes;
    for (const EntityPath& path : core::outermost(selectedPaths())) {
        const EntityItem* entityItem = item(path);
        Entity entity = entityItem->entity();
        const std::optional<core::TileGrid>& grid = entityItem->placement().frame.grid;
        for (int axis = 0; axis < 2; ++axis) {
            const int by = axis == 0 ? direction.x() : direction.y();
            core::Length& at = entity.at.axis(axis);
            if (at.unit == core::Unit::Cell && grid) {
                // From tile to tile, staying in the grid.
                const int tiles = axis == 0 ? grid->columns : grid->rows;
                at.value = std::clamp(at.value + by * count, 0.0, double(std::max(tiles - 1, 0)));
            }
            else {
                (axis == 0 ? entity.transform.pos.x : entity.transform.pos.y) += by * step;
            }
        }
        if (entity == entityItem->entity()) {
            continue;
        }
        changes.append({path, entity});
    }
    if (!changes.isEmpty()) {
        m_document.commands().push(std::make_unique<core::EditEntitiesCommand>(
            m_document, m_sceneId, changes, tr("Nudge %n entities", nullptr, int(changes.size()))));
    }
}

void SceneCanvas::commitMoves()
{
    QList<std::pair<EntityPath, Entity>> changes;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        const EntityItem* entityItem = it.value();
        const core::Point position = entityItem->entityPosition();
        if (position == entityItem->entity().transform.pos) {
            continue;
        }
        // On an axis in tiles, the tile nearest to where it was dropped, as it was placed
        // from its own.
        const Entity entity = core::movedTo(entityItem->placement().frame, entityItem->entity(),
                                            {entityItem->pos().x(), entityItem->pos().y()});
        changes.append({it.key(), entity});
    }
    if (changes.isEmpty()) {
        return;
    }
    std::sort(changes.begin(), changes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    const Entity& first = changes.first().second;
    const QString text = changes.size() == 1 ? tr("Move %1").arg(core::describe(first))
                                             : tr("Move %n entities", nullptr, int(changes.size()));
    m_document.commands().push(
        std::make_unique<core::EditEntitiesCommand>(m_document, m_sceneId, changes, text));
}

void SceneCanvas::commitTransform(const EntityPath& path, double rotation, core::Point scale,
                                  const QString& verb)
{
    const EntityItem* entityItem = item(path);
    if (!entityItem) {
        return;
    }
    Entity entity = entityItem->entity();
    if (entity.transform.rot == rotation && entity.transform.scale == scale) {
        return;
    }
    entity.transform.rot = rotation;
    entity.transform.scale = scale;
    m_document.commands().push(std::make_unique<core::EditEntitiesCommand>(
        m_document, m_sceneId, QList<std::pair<EntityPath, Entity>>{{path, entity}},
        QStringLiteral("%1 %2").arg(verb, core::describe(entity))));
}

void SceneCanvas::commitSize(const EntityPath& path, QSizeF size)
{
    EntityItem* entityItem = item(path);
    const Entity* current = scene() ? core::entityAt(*scene(), path) : nullptr;
    if (!entityItem || !current) {
        return;
    }
    Entity entity = *current;
    entity.children.clear();
    // In the units it had, so a percentage stays one.
    const core::Extent resized =
        core::extentIn(entity.size.value_or(core::Extent{}), {size.width(), size.height()},
                       entityItem->placement().frame);
    if (entity.size == resized) {
        entityItem->setEntity(entity);  // drops what the handle showed
        return;
    }
    entity.size = resized;
    m_document.commands().push(std::make_unique<core::EditEntitiesCommand>(
        m_document, m_sceneId, QList<std::pair<EntityPath, Entity>>{{path, entity}},
        tr("Resize %1").arg(core::describe(entity))));
}

void SceneCanvas::moveEntity(const EntityPath& from, const EntityPath& to)
{
    clearSelection();  // the paths will name other entities afterwards
    m_document.commands().push(
        std::make_unique<core::MoveEntityCommand>(m_document, m_sceneId, from, to, imageMetrics()));
    select({to});
}

void SceneCanvas::clearItems()
{
    // Removing selected items emits selectionChanged(), which places the handles.
    m_rotateHandle = nullptr;
    m_scaleHandle = nullptr;
    m_gridOverlay = nullptr;
    m_gridPaths.clear();
    clear();
    m_items.clear();
}

void SceneCanvas::createHandles()
{
    m_rotateHandle = new TransformHandle(*this, TransformHandle::Kind::Rotate);
    m_scaleHandle = new TransformHandle(*this, TransformHandle::Kind::Scale);
    addItem(m_rotateHandle);
    addItem(m_scaleHandle);
    m_gridOverlay = new TileGridOverlay;
    addItem(m_gridOverlay);
}

void SceneCanvas::updateHandles()
{
    if (!m_rotateHandle) {
        return;
    }
    const QList<EntityPath> selected = selectedPaths();
    const EntityPath target = selected.size() == 1 ? selected.first() : EntityPath();
    m_rotateHandle->attach(target);
    m_scaleHandle->attach(target);
    updateGrids();
}

void SceneCanvas::updateGrids()
{
    if (!m_gridOverlay) {
        return;
    }
    QList<EntityPath> candidates = selectedPaths();
    for (const EntityPath& path : selectedPaths()) {
        if (path.size() > 1) {
            candidates << path.first(path.size() - 1);
        }
    }
    if (!m_currentParent.isEmpty()) {
        candidates << m_currentParent;
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    QList<EntityPath> paths;
    QList<const EntityItem*> containers;
    for (const EntityPath& path : std::as_const(candidates)) {
        const EntityItem* container = item(path);
        if (container && container->tileGrid() && container->isVisible()) {
            paths << path;
            containers << container;
        }
    }
    m_gridPaths = paths;
    m_gridOverlay->showGrids(containers);
}

// --- visuals ---

SceneCanvas::SpriteVisual SceneCanvas::visualOf(const core::Image& image) const
{
    return image.isAnimation() ? animationVisual(image.sheet, image.animation)
                               : spriteVisual(image.sheet, image.sprite);
}

core::ImageMetricsFn SceneCanvas::imageMetrics() const
{
    return [this](const core::Image& image) -> std::optional<core::ImageMetrics> {
        const SpriteVisual visual = visualOf(image);
        if (!visual.sprite || visual.sprite->sourceSize.isEmpty()) {
            return std::nullopt;
        }
        const QSizeF size = assets::naturalSize(*visual.sprite, visual.drawScale);
        const QSize source = visual.sprite->sourceSize;
        return core::ImageMetrics{{size.width(), size.height()},
                                  {double(visual.sprite->pivot.x()) / source.width(),
                                   double(visual.sprite->pivot.y()) / source.height()}};
    };
}

QPixmap SceneCanvas::tintedAtlas(const SpriteVisual& visual, const core::Color& tint) const
{
    if (tint.red == 255 && tint.green == 255 && tint.blue == 255) {
        return visual.atlas;
    }
    const QString key = QStringLiteral("%1/%2/%3")
                            .arg(visual.sheet)
                            .arg(visual.atlasIndex)
                            .arg(core::Color{tint.red, tint.green, tint.blue, 255}.name());
    if (const auto it = m_tintedAtlases.constFind(key); it != m_tintedAtlases.cend()) {
        return *it;
    }
    // Channel by channel, as raylib's tint and SDL's color mod multiply.
    QImage image = visual.atlas.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            line[x] = qRgba(qRed(pixel) * tint.red / 255, qGreen(pixel) * tint.green / 255,
                            qBlue(pixel) * tint.blue / 255, qAlpha(pixel));
        }
    }
    const QPixmap tinted = QPixmap::fromImage(image);
    m_tintedAtlases.insert(key, tinted);
    return tinted;
}

SceneCanvas::SpriteVisual SceneCanvas::spriteVisual(const QString& sheet, const QString& path) const
{
    SpriteVisual visual;
    const std::shared_ptr<const assets::Sheet> loaded = m_sheets.sheet(sheet);
    if (!loaded) {
        return visual;
    }
    visual.sprite = loaded->sprite(path);
    if (!visual.sprite) {
        return visual;
    }
    visual.drawScale = 1.0 / loaded->scale;
    auto atlases = m_atlases.find(sheet);
    if (atlases == m_atlases.end()) {
        std::vector<QPixmap> pixmaps;
        for (const QImage& image : loaded->atlases) {
            pixmaps.push_back(QPixmap::fromImage(image));
        }
        atlases = m_atlases.insert(sheet, std::move(pixmaps));
    }
    if (size_t(visual.sprite->atlas) < atlases->size()) {
        visual.atlas = (*atlases)[size_t(visual.sprite->atlas)];
        visual.sheet = sheet;
        visual.atlasIndex = visual.sprite->atlas;
    }
    return visual;
}

SceneCanvas::SpriteVisual SceneCanvas::animationVisual(const QString& sheet,
                                                       const QString& name) const
{
    const std::shared_ptr<const assets::Sheet> loaded = m_sheets.sheet(sheet);
    const assets::Animation* animation = loaded ? loaded->animation(name) : nullptr;
    if (!animation || animation->frames.empty()) {
        return {};
    }
    const int frame = animation->frames.front();
    SpriteVisual visual = spriteVisual(sheet, loaded->sprites[size_t(frame)].path);
    visual.flipX = animation->flipX;
    visual.flipY = animation->flipY;
    return visual;
}

QColor SceneCanvas::classColor(const QStringList& classes, const QColor& fallback) const
{
    const auto& registry = m_document.project().classes;
    if (registry) {
        for (const QString& name : classes) {
            const QColor color(registry->classes.value(name).color);
            if (color.isValid()) {
                return color;
            }
        }
    }
    return fallback;
}

QSizeF SceneCanvas::designResolution() const
{
    const core::Size size = m_document.project().manifest.designResolution;
    return {double(size.width), double(size.height)};
}

void SceneCanvas::onSheetChanged(const QString& alias)
{
    m_atlases.remove(alias);
    m_tintedAtlases.removeIf(
        [&](const auto& entry) { return entry.key().startsWith(alias + u'/'); });
    for (EntityItem* entityItem : std::as_const(m_items)) {
        entityItem->refreshVisual();
    }
    updateHandles();
}

// --- items ---

void SceneCanvas::rebuild()
{
    const core::Scene* current = scene();
    if (!current) {
        m_treeShape.clear();
        clearItems();
        createHandles();
        emit contentChanged();
        return;
    }
    // The current parent is found by its id where it moved, if it has one.
    if (!m_currentParentId.isEmpty()) {
        const EntityPath found = core::findEntity(*current, m_currentParentId);
        if (found != m_currentParent) {
            m_currentParent = found;  // empty, the top level, once it is removed
            if (found.isEmpty()) {
                m_currentParentId.clear();
            }
            emit currentParentChanged();
        }
    }
    else if (!m_currentParent.isEmpty() && !core::entityAt(*current, m_currentParent)) {
        setCurrentParent({});  // it was removed
    }

    // Field changes, such as the end of a drag, update the items in place: the item that
    // started the drag is still handling its mouse event.
    const QStringList shape = treeShape(*current);
    EntityPath path;
    if (shape == m_treeShape) {
        placeItems(current->entities, path, core::sceneFrame(*current), nullptr, core::Color{},
                   false, false, false);
        updateHandles();
        emit contentChanged();
        return;
    }
    m_treeShape = shape;

    const QList<EntityPath> selected = selectedPaths();
    clearItems();
    createHandles();
    placeItems(current->entities, path, core::sceneFrame(*current), nullptr, core::Color{}, false,
               false, true);
    select(selected);
    emit contentChanged();
}

void SceneCanvas::placeItems(const std::vector<Entity>& entities, EntityPath& path,
                             const core::Frame& frame, EntityItem* parent, const core::Color& tint,
                             bool screen, bool blocked, bool create)
{
    const core::ImageMetricsFn metrics = imageMetrics();
    for (size_t i = 0; i < entities.size(); ++i) {
        const Entity& entity = entities[i];
        path.append(int(i));
        const core::Point offset = core::anchorPoint(frame, entity.at);
        const bool inScreen = entity.space ? *entity.space == core::Space::Screen : screen;
        const EntityItem::Placement placement{
            {offset.x, offset.y}, core::effectiveTint(entity, tint), inScreen, frame};
        const core::Frame children = core::childFrame(entity, frame, metrics);
        EntityItem* item = nullptr;
        if (create) {
            item = new EntityItem(*this, path, entity, placement, parent);
            if (!parent) {
                addItem(item);
            }
            // Guides only exist in the editor, so they go under what the game draws among
            // their siblings; otherwise a room-sized region would catch every click.
            item->setZValue(double(i) - (item->isGuide() ? double(entities.size()) : 0));
            item->setVisible(!inScreen && !isHidden(path));
            const bool locked = blocked || isLocked(path) || inScreen;
            if (locked) {
                item->setFlags(item->flags() &
                               ~(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable));
            }
            m_items.insert(path, item);
            placeItems(entity.children, path, children, item, placement.tint, inScreen, locked,
                       true);
        }
        else {
            item = m_items.value(path);
            if (item) {
                item->setEntity(entity, placement);
            }
            placeItems(entity.children, path, children, item, placement.tint, inScreen, blocked,
                       false);
        }
        path.removeLast();
    }
}

}  // namespace shoal::editor
