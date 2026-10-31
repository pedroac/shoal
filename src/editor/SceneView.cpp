#include "editor/SceneView.h"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <cmath>

#include "assets/SpriteSheet.h"
#include "editor/EntityItem.h"
#include "editor/PresetsPanel.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

namespace {
constexpr double PRESET_CROSS = 24;  ///< On screen, in pixels.
}  // namespace

SceneView::SceneView(core::ProjectDocument& document, assets::SheetCache& sheets,
                     assets::FontCache& fonts, const QString& sceneId, QWidget* parent)
    : CanvasView(nullptr, parent),
      m_canvas(std::make_unique<SceneCanvas>(document, sheets, fonts, sceneId))
{
    setScene(m_canvas.get());
    watchScene(m_canvas.get());
    setBackgroundBrush(palette().color(QPalette::Dark));
}

SceneView::~SceneView()
{
    setScene(nullptr);
}

core::Entity SceneView::entityFor(const AssetItem& item) const
{
    core::Entity entity;
    if (item.kind == AssetItem::Kind::Font) {
        // "Text" in a rect sized to it, centered on its origin.
        core::Text text;
        text.font = item.alias;
        text.string = tr("Text");
        text.wrap = core::TextWrap::None;
        const TextLayout layout = m_canvas->layOutText(text, 0);
        text.wrap = core::TextWrap::Word;
        entity.text = text;
        entity.size =
            core::Extent::px({std::ceil(layout.size.width()), std::ceil(layout.size.height())});
        entity.pivot = core::Point{0.5, 0.5};
        return entity;
    }
    if (item.kind == AssetItem::Kind::Audio) {
        // A sound source where it is, heard everywhere until given a radius.
        core::Audio audio;
        audio.clip = item.alias;
        entity.audio = audio;
        return entity;
    }
    if (item.kind == AssetItem::Kind::Video) {
        core::Video video;
        video.clip = item.alias;
        entity.video = video;
        entity.size = core::Extent::px(m_canvas->newVideoSize());
        entity.pivot = core::Point{0.5, 0.5};
        return entity;
    }
    entity.image = item.toImage();
    // A sprite with nine-slice insets is placed as a nine-slice, its natural size set so
    // the scale handle resizes it.
    if (!entity.image->isAnimation()) {
        const SceneCanvas::SpriteVisual visual = m_canvas->visualOf(*entity.image);
        if (visual.sprite && visual.sprite->hasSlice) {
            entity.image->mode = core::ImageMode::NineSlice;
            const QSizeF size = assets::naturalSize(*visual.sprite, visual.drawScale);
            entity.size = core::Extent::px({size.width(), size.height()});
        }
    }
    return entity;
}

core::EntityPath SceneView::place(const AssetItem& item, QPointF scenePosition)
{
    const core::EntityPath path = m_canvas->addEntity(entityFor(item), scenePosition);
    if (!path.isEmpty()) {
        setFocus();  // so Delete and the arrow keys reach the new entity
    }
    return path;
}

core::EntityPath SceneView::placeAtCenter(const AssetItem& item)
{
    return place(item, visibleCenter());
}

QList<core::EntityPath> SceneView::placePreset(const QString& presetId, QPointF scenePosition,
                                               QString* error)
{
    const QList<core::EntityPath> paths = m_canvas->addPreset(presetId, scenePosition, error);
    if (!paths.isEmpty()) {
        setFocus();
    }
    return paths;
}

QPointF SceneView::visibleCenter() const
{
    return mapToScene(viewport()->rect().center());
}

QRectF SceneView::contentRect() const
{
    return m_canvas->sceneBounds().united(m_canvas->itemsBoundingRect());
}

void SceneView::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, palette().color(QPalette::Dark));
    if (m_canvas->presetId()) {
        // A preset has no rect: a cross where it is placed.
        const double arm = PRESET_CROSS / zoom();
        painter->setPen(QPen(palette().color(QPalette::Midlight), 0));
        painter->drawLine(QPointF(-arm, 0), QPointF(arm, 0));
        painter->drawLine(QPointF(0, -arm), QPointF(0, arm));
        return;
    }
    const QRectF bounds = m_canvas->sceneBounds();
    painter->fillRect(bounds, palette().color(QPalette::Base));

    painter->setPen(QPen(palette().color(QPalette::Mid), 0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bounds);
}

void SceneView::keyPressEvent(QKeyEvent* event)
{
    QPoint direction;
    switch (event->key()) {
        case Qt::Key_Left:
            direction = {-1, 0};
            break;
        case Qt::Key_Right:
            direction = {1, 0};
            break;
        case Qt::Key_Up:
            direction = {0, -1};
            break;
        case Qt::Key_Down:
            direction = {0, 1};
            break;
        default:
            CanvasView::keyPressEvent(event);
            return;
    }
    m_canvas->nudgeSelection(direction, event->modifiers().testFlag(Qt::ShiftModifier));
    event->accept();
}

void SceneView::contextMenuEvent(QContextMenuEvent* event)
{
    // The entity under the cursor, or the one an item under it belongs to.
    QGraphicsItem* hit = itemAt(event->pos());
    while (hit && !qgraphicsitem_cast<EntityItem*>(hit)) {
        hit = hit->parentItem();
    }
    auto* entity = qgraphicsitem_cast<EntityItem*>(hit);
    if (!entity || !(entity->flags() & QGraphicsItem::ItemIsSelectable)) {
        CanvasView::contextMenuEvent(event);
        return;
    }
    if (!entity->isSelected()) {
        m_canvas->select({entity->path()});
    }
    QMenu menu(this);
    m_canvas->addArrangeActions(menu);
    menu.addSeparator();
    menu.addAction(tr("Make Preset"), m_canvas.get(), &SceneCanvas::makePresetFromSelection);
    menu.addAction(tr("Remove"), m_canvas.get(), &SceneCanvas::removeSelection);
    menu.exec(event->globalPos());
}

// --- drops from the sheet browser and the presets ---

void SceneView::dragEnterEvent(QDragEnterEvent* event)
{
    dragMoveEvent(event);
}

void SceneView::dragMoveEvent(QDragMoveEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!AssetItem::fromMimeData(mime) && !PresetsPanel::presetFromMimeData(mime)) {
        CanvasView::dragMoveEvent(event);
        return;
    }
    // Refused into a hidden or locked parent, where the new entity couldn't be seen or picked.
    if (m_canvas->canAddEntities()) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void SceneView::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    // The drop is in viewport pixels; mapToScene() undoes the zoom and the pan.
    const QPointF at = mapToScene(event->position().toPoint());
    bool placed = false;
    if (const std::optional<AssetItem> item = AssetItem::fromMimeData(mime)) {
        placed = !place(*item, at).isEmpty();
    }
    else if (const std::optional<QString> preset = PresetsPanel::presetFromMimeData(mime)) {
        placed = !placePreset(*preset, at).isEmpty();
    }
    else {
        CanvasView::dropEvent(event);
        return;
    }
    if (placed) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

}  // namespace shoal::editor
