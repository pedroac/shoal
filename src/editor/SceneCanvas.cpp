#include "editor/SceneCanvas.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <algorithm>
#include <cmath>
#include <memory>

#include "assets/SheetCache.h"
#include "core/CommandStack.h"
#include "core/Presets.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/EntityItem.h"
#include "editor/TransformHandle.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

/// Layers sit far apart in z, so an entity's z can be its draw order within its layer.
constexpr double LAYER_Z_STEP = 1e6;

/// The shape of a scene's tree: what decides whether the items can be updated in place.
QStringList treeShape(const core::Scene& scene)
{
    QStringList shape;
    for (const core::Layer& layer : scene.layers) {
        shape << QStringLiteral("L%1:%2").arg(layer.id).arg(int(layer.space));
    }
    core::forEachEntity(scene, [&](const Entity& entity, const EntityPath& path) {
        QStringList indexes;
        for (int index : path) {
            indexes << QString::number(index);
        }
        // The type too: regions, cameras and paths take a different z value.
        shape << QStringLiteral("%1:%2:%3")
                     .arg(indexes.join(u'.'), entity.layer)
                     .arg(int(entity.type));
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

    if (const core::Scene* current = scene()) {
        for (auto it = current->layers.rbegin(); it != current->layers.rend(); ++it) {
            if (it->space == core::Space::World) {
                m_currentLayer = it->id;  // the front-most world layer
                break;
            }
        }
    }
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

void SceneCanvas::setCurrentLayer(const QString& layerId)
{
    if (m_currentLayer != layerId) {
        m_currentLayer = layerId;
        emit currentLayerChanged(layerId);
    }
}

void SceneCanvas::setLayerHidden(const QString& layerId, bool hidden)
{
    if (hidden == m_hidden.contains(layerId)) {
        return;
    }
    if (hidden) {
        m_hidden.insert(layerId);
    }
    else {
        m_hidden.remove(layerId);
    }
    m_treeShape.clear();  // the items' flags and visibility change
    rebuild();
    emit layerStateChanged();
}

void SceneCanvas::setLayerLocked(const QString& layerId, bool locked)
{
    if (locked == m_locked.contains(layerId)) {
        return;
    }
    if (locked) {
        m_locked.insert(layerId);
    }
    else {
        m_locked.remove(layerId);
    }
    m_treeShape.clear();  // the items' flags and visibility change
    rebuild();
    emit layerStateChanged();
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

bool SceneCanvas::canAddEntities() const
{
    const core::Scene* current = scene();
    return current && core::layerIndex(*current, m_currentLayer) >= 0 &&
           !isLayerHidden(m_currentLayer) && !isLayerLocked(m_currentLayer);
}

EntityPath SceneCanvas::addEntity(Entity entity, QPointF scenePosition)
{
    const core::Scene* current = scene();
    if (!canAddEntities()) {
        return {};
    }
    const QPointF at = snap(scenePosition);
    entity.layer = m_currentLayer;
    entity.transform.pos = core::Point{at.x(), at.y()};
    const EntityPath path{int(current->entities.size())};
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
    const QPointF at = snap(scenePosition);
    std::vector<Entity> entities =
        core::instantiatePreset(*read.definition, *current, m_currentLayer, {at.x(), at.y()});
    QList<EntityPath> paths;
    int index = int(current->entities.size());
    core::CommandStack& commands = m_document.commands();
    commands.beginMacro(tr("Add %1").arg(read.definition->name));
    for (Entity& entity : entities) {
        paths << EntityPath{index++};
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

void SceneCanvas::nudgeSelection(QPointF delta)
{
    QList<std::pair<EntityPath, Entity>> changes;
    for (const EntityPath& path : core::outermost(selectedPaths())) {
        Entity entity = item(path)->entity();
        entity.transform.pos.x += delta.x();
        entity.transform.pos.y += delta.y();
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
        const core::Point stored = entityItem->entity().transform.pos;
        const QPointF position = entityItem->pos();
        if (position != QPointF(stored.x, stored.y)) {
            Entity entity = entityItem->entity();
            entity.transform.pos = core::Point{position.x(), position.y()};
            changes.append({it.key(), entity});
        }
    }
    if (changes.isEmpty()) {
        return;
    }
    std::sort(changes.begin(), changes.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    const Entity& first = changes.first().second;
    const QString text =
        changes.size() == 1
            ? tr("Move %1").arg(first.id.isEmpty() ? core::enumName(first.type) : first.id)
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
    const QString name = entity.id.isEmpty() ? core::enumName(entity.type) : entity.id;
    m_document.commands().push(std::make_unique<core::EditEntitiesCommand>(
        m_document, m_sceneId, QList<std::pair<EntityPath, Entity>>{{path, entity}},
        QStringLiteral("%1 %2").arg(verb, name)));
}

void SceneCanvas::clearItems()
{
    // Removing selected items emits selectionChanged(), which places the handles.
    m_rotateHandle = nullptr;
    m_scaleHandle = nullptr;
    clear();
    m_items.clear();
    m_layerItems.clear();
}

void SceneCanvas::createHandles()
{
    m_rotateHandle = new TransformHandle(*this, TransformHandle::Kind::Rotate);
    m_scaleHandle = new TransformHandle(*this, TransformHandle::Kind::Scale);
    addItem(m_rotateHandle);
    addItem(m_scaleHandle);
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
}

// --- visuals ---

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

    // Field changes, such as the end of a drag, update the items in place: the item that
    // started the drag is still handling its mouse event.
    const QStringList shape = treeShape(*current);
    if (shape == m_treeShape) {
        core::forEachEntity(*current, [&](const Entity& entity, const EntityPath& path) {
            if (EntityItem* entityItem = item(path)) {
                entityItem->setEntity(entity);
            }
        });
        updateHandles();
        emit contentChanged();
        return;
    }
    m_treeShape = shape;

    const QList<EntityPath> selected = selectedPaths();
    clearItems();
    createHandles();
    for (size_t i = 0; i < current->layers.size(); ++i) {
        const core::Layer& layer = current->layers[i];
        auto* layerItem = new QGraphicsRectItem();  // an empty rect draws nothing
        layerItem->setPen(Qt::NoPen);
        layerItem->setZValue(double(i) * LAYER_Z_STEP);
        layerItem->setVisible(layer.space == core::Space::World && !m_hidden.contains(layer.id));
        addItem(layerItem);
        m_layerItems.push_back(layerItem);
    }
    for (size_t i = 0; i < current->entities.size(); ++i) {
        const Entity& entity = current->entities[i];
        const int layer = core::layerIndex(*current, entity.layer);
        if (layer < 0) {
            continue;  // on a missing layer: the Problems panel's to report
        }
        EntityPath path{int(i)};
        auto* top = new EntityItem(*this, path, entity, m_layerItems[size_t(layer)]);
        // Regions, cameras and paths only exist in the editor, so they go under what the
        // game draws; otherwise a room-sized region would catch every click.
        const bool guide = entity.type == core::EntityType::Region ||
                           entity.type == core::EntityType::Camera ||
                           entity.type == core::EntityType::Path;
        top->setZValue(double(i) - (guide ? double(current->entities.size()) : 0));
        const bool locked = m_locked.contains(entity.layer);
        if (locked) {
            top->setFlags(top->flags() &
                          ~(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable));
        }
        m_items.insert(path, top);
        addItems(entity.children, path, top, locked);
    }
    select(selected);
    emit contentChanged();
}

void SceneCanvas::addItems(const std::vector<Entity>& entities, EntityPath& path,
                           QGraphicsItem* parent, bool locked)
{
    for (size_t i = 0; i < entities.size(); ++i) {
        path.append(int(i));
        auto* child = new EntityItem(*this, path, entities[i], parent);
        if (locked) {
            child->setFlags(child->flags() &
                            ~(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable));
        }
        m_items.insert(path, child);
        addItems(entities[i].children, path, child, locked);
        path.removeLast();
    }
}

}  // namespace shoal::editor
