#include "editor/TransformHandle.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <cmath>

#include "editor/EntityItem.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

namespace {
constexpr double HANDLE_SIZE = 10;    ///< On screen, in pixels.
constexpr double ROTATE_OFFSET = 24;  ///< How far above the outline the rotate handle sits.
constexpr double ANGLE_SNAP = 15;
constexpr double MIN_SCALE = 1e-3;

double angleTo(QPointF from, QPointF to)
{
    return qRadiansToDegrees(std::atan2(to.y() - from.y(), to.x() - from.x()));
}
}  // namespace

TransformHandle::TransformHandle(SceneCanvas& canvas, Kind kind) : m_canvas(canvas), m_kind(kind)
{
    setFlag(ItemIgnoresTransformations);
    setZValue(1e12);  // above every layer
    setCursor(kind == Kind::Rotate ? Qt::CrossCursor : Qt::SizeFDiagCursor);
    hide();
}

QRectF TransformHandle::boundingRect() const
{
    const double h = HANDLE_SIZE / 2 + 1;
    return m_kind == Kind::Rotate ? QRectF(-h, -ROTATE_OFFSET - h, 2 * h, ROTATE_OFFSET + 2 * h)
                                  : QRectF(-h, -h, 2 * h, 2 * h);
}

void TransformHandle::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                            QWidget* widget)
{
    Q_UNUSED(widget);
    const QColor color = option->palette.color(QPalette::Highlight);
    painter->setPen(QPen(color, 1.5));
    painter->setBrush(option->palette.color(QPalette::Base));
    const double h = HANDLE_SIZE / 2;
    if (m_kind == Kind::Rotate) {
        painter->drawLine(QPointF(0, 0), QPointF(0, -ROTATE_OFFSET + h));
        painter->drawEllipse(QPointF(0, -ROTATE_OFFSET), h, h);
    }
    else {
        painter->drawRect(QRectF(-h, -h, 2 * h, 2 * h));
    }
}

void TransformHandle::attach(const core::EntityPath& target)
{
    m_target = target;
    const EntityItem* item = target.isEmpty() ? nullptr : m_canvas.item(target);
    if (!item || !item->isVisible()) {
        hide();
        return;
    }
    const QRectF rect = item->visualRect();
    setPos(item->mapToScene(m_kind == Kind::Rotate ? QPointF(rect.center().x(), rect.top())
                                                   : rect.bottomRight()));
    show();
}

void TransformHandle::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    EntityItem* item = m_canvas.item(m_target);
    if (event->button() != Qt::LeftButton || !item) {
        event->ignore();
        return;
    }
    m_origin = item->scenePos();
    m_startAngle = angleTo(m_origin, event->scenePos());
    m_startDistance = QLineF(m_origin, event->scenePos()).length();
    m_rotation = item->entity().transform.rot;
    m_scale = item->entity().transform.scale;
    event->accept();
}

void TransformHandle::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    EntityItem* item = m_canvas.item(m_target);
    if (!item) {
        return;
    }
    const core::Transform start = item->entity().transform;
    if (m_kind == Kind::Rotate) {
        double rotation = start.rot + angleTo(m_origin, event->scenePos()) - m_startAngle;
        if (event->modifiers() & Qt::ShiftModifier) {
            rotation = std::round(rotation / ANGLE_SNAP) * ANGLE_SNAP;
        }
        m_rotation = std::remainder(rotation, 360.0);
    }
    else if (m_startDistance > 0) {
        const double factor = QLineF(m_origin, event->scenePos()).length() / m_startDistance;
        const auto scaled = [&](double value) {
            const double result = value * factor;
            return std::abs(result) < MIN_SCALE ? std::copysign(MIN_SCALE, value) : result;
        };
        m_scale = core::Point{scaled(start.scale.x), scaled(start.scale.y)};
    }
    item->setTransform(QTransform::fromScale(m_scale.x, m_scale.y) *
                       QTransform().rotate(m_rotation));
    attach(m_target);
}

void TransformHandle::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    Q_UNUSED(event);
    m_canvas.commitTransform(
        m_target, m_rotation, m_scale,
        m_kind == Kind::Rotate ? SceneCanvas::tr("Rotate") : SceneCanvas::tr("Scale"));
}

}  // namespace shoal::editor
