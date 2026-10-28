#include "editor/SceneView.h"

#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <cmath>

#include "editor/PresetsPanel.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

namespace {
constexpr double MIN_GRID_PIXELS = 12;  ///< Finer grids aren't drawn.
constexpr double NUDGE_MULTIPLIER = 10;
}  // namespace

SceneView::SceneView(core::ProjectDocument& document, assets::SheetCache& sheets,
                     const QString& sceneId, QWidget* parent)
    : CanvasView(nullptr, parent),
      m_canvas(std::make_unique<SceneCanvas>(document, sheets, sceneId))
{
    setScene(m_canvas.get());
    watchScene(m_canvas.get());
    setBackgroundBrush(palette().color(QPalette::Dark));
}

SceneView::~SceneView()
{
    setScene(nullptr);
}

core::EntityPath SceneView::place(const SheetItem& item, QPointF scenePosition)
{
    const core::EntityPath path = m_canvas->addEntity(item.toEntity(), scenePosition);
    if (!path.isEmpty()) {
        setFocus();  // so Delete and the arrow keys reach the new entity
    }
    return path;
}

core::EntityPath SceneView::placeAtCenter(const SheetItem& item)
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
    const QRectF bounds = m_canvas->sceneBounds();
    painter->fillRect(bounds, palette().color(QPalette::Base));

    // The grid, inside the scene, when its cells are big enough on screen to help.
    const double grid = m_canvas->gridSize();
    if (grid * zoom() >= MIN_GRID_PIXELS) {
        const QRectF area = rect.intersected(bounds);
        QPen pen(palette().color(QPalette::Midlight), 0);
        painter->setPen(pen);
        QVector<QLineF> lines;
        for (double x = std::ceil(area.left() / grid) * grid; x <= area.right(); x += grid) {
            lines << QLineF(x, area.top(), x, area.bottom());
        }
        for (double y = std::ceil(area.top() / grid) * grid; y <= area.bottom(); y += grid) {
            lines << QLineF(area.left(), y, area.right(), y);
        }
        painter->drawLines(lines);
    }
    painter->setPen(QPen(palette().color(QPalette::Mid), 0));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bounds);
}

void SceneView::keyPressEvent(QKeyEvent* event)
{
    QPointF direction;
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
    double step = m_canvas->snapping() ? m_canvas->gridSize() : 1;
    if (event->modifiers() & Qt::ShiftModifier) {
        step *= NUDGE_MULTIPLIER;
    }
    m_canvas->nudgeSelection(direction * step);
    event->accept();
}

// --- drops from the sheet browser and the presets ---

void SceneView::dragEnterEvent(QDragEnterEvent* event)
{
    dragMoveEvent(event);
}

void SceneView::dragMoveEvent(QDragMoveEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!SheetItem::fromMimeData(mime) && !PresetsPanel::presetFromMimeData(mime)) {
        CanvasView::dragMoveEvent(event);
        return;
    }
    // Refused on a hidden or locked layer, where the new entity couldn't be seen or picked.
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
    if (const std::optional<SheetItem> item = SheetItem::fromMimeData(mime)) {
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
