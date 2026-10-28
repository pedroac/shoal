#include "editor/CanvasView.h"

#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace shoal::editor {

namespace {
constexpr double MIN_ZOOM = 0.02;
constexpr double MAX_ZOOM = 32;
constexpr double ZOOM_STEP = 1.25;
constexpr double WHEEL_ZOOM_BASE = 1.0015;  ///< Per eighth of a degree of wheel rotation.
constexpr double FIT_MARGIN = 40;
}  // namespace

CanvasView::CanvasView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    if (scene) {
        watchScene(scene);
    }
}

void CanvasView::watchScene(QGraphicsScene* scene)
{
    connect(scene, &QGraphicsScene::changed, this, &CanvasView::updateSceneRect);
}

QRectF CanvasView::contentRect() const
{
    return scene()->itemsBoundingRect();
}

void CanvasView::updateSceneRect()
{
    // Room to drag and pan past the content, without jumping when content shrinks.
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const QRectF rect =
        contentRect()
            .adjusted(-visible.width(), -visible.height(), visible.width(), visible.height())
            .united(visible);
    if (rect != sceneRect()) {
        setSceneRect(rect);
    }
}

double CanvasView::zoom() const
{
    return std::hypot(transform().m11(), transform().m12());
}

void CanvasView::setZoom(double zoom)
{
    const double factor = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM) / this->zoom();
    scale(factor, factor);
}

void CanvasView::zoomIn()
{
    setZoom(zoom() * ZOOM_STEP);
}

void CanvasView::zoomOut()
{
    setZoom(zoom() / ZOOM_STEP);
}

void CanvasView::resetZoom()
{
    setZoom(1);
}

void CanvasView::fitAll()
{
    const QRectF content = contentRect();
    if (content.isEmpty()) {
        resetZoom();
        return;
    }
    updateSceneRect();
    fitInView(content.adjusted(-FIT_MARGIN, -FIT_MARGIN, FIT_MARGIN, FIT_MARGIN),
              Qt::KeepAspectRatio);
    if (zoom() > 1) {  // small content is shown at its size, not blown up
        resetZoom();
    }
    else if (zoom() < MIN_ZOOM) {
        setZoom(MIN_ZOOM);
    }
    centerOn(content.center());
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        setZoom(zoom() * std::pow(WHEEL_ZOOM_BASE, event->angleDelta().y()));
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void CanvasView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panFrom = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint position = event->position().toPoint();
        const QPoint delta = position - m_panFrom;
        m_panFrom = position;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

}  // namespace shoal::editor
