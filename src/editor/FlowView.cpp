#include "editor/FlowView.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "core/Model.h"
#include "core/ProjectDocument.h"
#include "editor/FlowScene.h"
#include "editor/SceneNodeItem.h"
#include "editor/TransitionEdgeItem.h"

namespace shoal::editor {

namespace {
constexpr double MIN_ZOOM = 0.1;
constexpr double MAX_ZOOM = 4;
constexpr double ZOOM_STEP = 1.25;
constexpr double WHEEL_ZOOM_BASE = 1.0015;  ///< Per eighth of a degree of wheel rotation.
constexpr double FIT_MARGIN = 40;
constexpr double NEW_SCENE_NUDGE = 24;
}  // namespace

FlowView::FlowView(FlowScene& flowScene, QWidget* parent)
    : QGraphicsView(&flowScene, parent), m_flowScene(flowScene)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);

    // Room to drag and pan past the nodes, without jumping when content shrinks.
    connect(&m_flowScene, &QGraphicsScene::changed, this, [this] {
        const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
        const QRectF content = m_flowScene.itemsBoundingRect().adjusted(
            -visible.width(), -visible.height(), visible.width(), visible.height());
        const QRectF rect = content.united(visible);
        if (rect != sceneRect()) {
            setSceneRect(rect);
        }
    });
}

double FlowView::zoom() const
{
    return transform().m11();
}

void FlowView::setZoom(double zoom)
{
    const double clamped = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
    const double factor = clamped / this->zoom();
    scale(factor, factor);
}

void FlowView::zoomIn()
{
    setZoom(zoom() * ZOOM_STEP);
}

void FlowView::zoomOut()
{
    setZoom(zoom() / ZOOM_STEP);
}

void FlowView::resetZoom()
{
    setZoom(1);
}

void FlowView::fitAll()
{
    const QRectF content = m_flowScene.itemsBoundingRect();
    if (content.isEmpty()) {
        resetZoom();
        return;
    }
    fitInView(content.adjusted(-FIT_MARGIN, -FIT_MARGIN, FIT_MARGIN, FIT_MARGIN),
              Qt::KeepAspectRatio);
    if (zoom() > 1) {  // a small graph is shown at its size, not blown up
        resetZoom();
    }
    else if (zoom() < MIN_ZOOM) {
        setZoom(MIN_ZOOM);
    }
    centerOn(content.center());
}

void FlowView::addScene()
{
    QPointF position = mapToScene(viewport()->rect().center()) -
                       QPointF(SceneNodeItem::WIDTH / 2, NEW_SCENE_NUDGE);
    position = QPointF(std::round(position.x()), std::round(position.y()));
    // Don't hide a node exactly under another one, as repeated adds would.
    const auto& positions = m_flowScene.document().project().flow.nodePositions;
    while (std::any_of(positions.cbegin(), positions.cend(), [&](const core::Point& p) {
        return p.x == position.x() && p.y == position.y();
    })) {
        position += QPointF(NEW_SCENE_NUDGE, NEW_SCENE_NUDGE);
    }
    m_flowScene.addScene(position);
}

void FlowView::renameSelectedScene()
{
    const QStringList selected = m_flowScene.selectedSceneIds();
    if (selected.size() == 1) {
        promptRename(selected.first());
    }
}

void FlowView::setSelectedSceneAsStart()
{
    const QStringList selected = m_flowScene.selectedSceneIds();
    if (selected.size() == 1) {
        m_flowScene.setStartScene(selected.first());
    }
}

void FlowView::removeSelection()
{
    m_flowScene.removeSelection();
}

void FlowView::promptRename(const QString& sceneId)
{
    QString text = sceneId;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getText(this, tr("Rename Scene"),
                                     tr("New id for scene %1 (also its folder name):").arg(sceneId),
                                     QLineEdit::Normal, text, &ok)
                   .trimmed();
        if (!ok || m_flowScene.renameScene(sceneId, text)) {
            return;
        }
        const QString reason =
            core::isValidId(text)
                ? tr("Another scene is already called %1. Scene ids are compared ignoring case, "
                     "since they name folders.")
                      .arg(text)
                : tr("An id starts with a letter or an underscore, followed by letters, digits, "
                     "underscores or hyphens.");
        QMessageBox::warning(this, tr("Rename Scene"), reason);
    }
}

void FlowView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        setZoom(zoom() * std::pow(WHEEL_ZOOM_BASE, event->angleDelta().y()));
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void FlowView::mousePressEvent(QMouseEvent* event)
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

void FlowView::mouseMoveEvent(QMouseEvent* event)
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

void FlowView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void FlowView::contextMenuEvent(QContextMenuEvent* event)
{
    const QPointF scenePosition = mapToScene(event->pos());
    QGraphicsItem* item = m_flowScene.itemAt(scenePosition);
    if (item && !item->isSelected()) {
        m_flowScene.clearSelection();
        item->setSelected(true);
    }

    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Add Scene Here"), this, [this, scenePosition] {
            m_flowScene.addScene(scenePosition -
                                 QPointF(SceneNodeItem::WIDTH / 2, NEW_SCENE_NUDGE));
        });
    }
    else if (item->type() == SceneNodeItem::Type) {
        const QString id = static_cast<SceneNodeItem*>(item)->sceneId();
        const bool single = m_flowScene.selectedSceneIds().size() == 1 &&
                            m_flowScene.selectedTransitionIds().isEmpty();
        if (single) {
            menu.addAction(tr("Rename…"), this, [this, id] { promptRename(id); });
            QAction* start = menu.addAction(tr("Set as Start Scene"), this,
                                            [this, id] { m_flowScene.setStartScene(id); });
            start->setEnabled(m_flowScene.document().project().manifest.start != id);
            QMenu* connectMenu = menu.addMenu(tr("Add Transition To"));
            for (const QString& to : m_flowScene.document().project().scenes.keys()) {
                connectMenu->addAction(to, this,
                                       [this, id, to] { m_flowScene.addTransition(id, to); });
            }
            menu.addSeparator();
        }
        menu.addAction(tr("Remove"), this, &FlowView::removeSelection);
    }
    else {
        menu.addAction(tr("Remove"), this, &FlowView::removeSelection);
    }
    menu.exec(event->globalPos());
}

}  // namespace shoal::editor
