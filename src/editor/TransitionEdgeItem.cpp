#include "editor/TransitionEdgeItem.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <limits>

#include "editor/FlowScene.h"
#include "editor/SceneNodeItem.h"

namespace shoal::editor {

namespace {
constexpr double ARROW_LENGTH = 10;
constexpr double ARROW_HALF_WIDTH = 5;
constexpr double LOOP_HEIGHT = 48;
constexpr double LOOP_STEP = 24;
constexpr double PICK_WIDTH = 12;  ///< How close a click must be to select the edge.
constexpr double LABEL_PADDING = 4;

QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::editor::TransitionEdgeItem", text);
}

/// Where the line from the center of `rect` toward `toward` leaves the rectangle.
QPointF exitPoint(const QRectF& rect, QPointF toward)
{
    const QPointF center = rect.center();
    const QPointF d = toward - center;
    constexpr double INF = std::numeric_limits<double>::infinity();
    const double tx = qFuzzyIsNull(d.x()) ? INF : rect.width() / 2 / qAbs(d.x());
    const double ty = qFuzzyIsNull(d.y()) ? INF : rect.height() / 2 / qAbs(d.y());
    const double t = qMin(tx, ty);
    return t == INF ? center : center + d * t;
}

QPolygonF arrowHead(QPointF tip, QPointF direction)
{
    const double length = std::hypot(direction.x(), direction.y());
    if (qFuzzyIsNull(length)) {
        return {};
    }
    const QPointF unit = direction / length;
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = tip - unit * ARROW_LENGTH;
    return QPolygonF({tip, base + normal * ARROW_HALF_WIDTH, base - normal * ARROW_HALF_WIDTH});
}

}  // namespace

TransitionEdgeItem::TransitionEdgeItem(FlowScene& flowScene, QString transitionId)
    : m_flowScene(flowScene), m_transitionId(std::move(transitionId))
{
    setFlag(ItemIsSelectable);
    setZValue(0);
}

void TransitionEdgeItem::setTransition(const core::Transition& transition, double bend)
{
    m_from = transition.from;
    m_to = transition.to;
    m_mode = transition.mode;
    m_bend = bend;

    QStringList tip{QStringLiteral("<b>%1</b>").arg(transition.id.toHtmlEscaped()),
                    tr("Mode: %1").arg(core::enumName(transition.mode))};
    if (!transition.trigger.event.isEmpty()) {
        tip << tr("Event: %1").arg(transition.trigger.event.toHtmlEscaped());
    }
    if (!transition.trigger.requirements.isEmpty()) {
        tip << tr("Requires: %1")
                   .arg(transition.trigger.requirements.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    setToolTip(tip.join(QStringLiteral("<br>")));
    updateGeometry();
}

void TransitionEdgeItem::updateGeometry()
{
    prepareGeometryChange();
    m_path = {};
    m_arrow = {};
    m_labelRect = {};

    const SceneNodeItem* fromNode = m_flowScene.node(m_from);
    const SceneNodeItem* toNode = m_flowScene.node(m_to);
    if (!fromNode || !toNode) {
        return;
    }
    const QRectF a = fromNode->mapRectToScene(fromNode->bodyRect());
    const QRectF b = toNode->mapRectToScene(toNode->bodyRect());

    QPointF labelAt;
    if (fromNode == toNode) {
        const double height = LOOP_HEIGHT + m_bend * LOOP_STEP;
        const QPointF start(a.left() + a.width() * 0.3, a.top());
        const QPointF end(a.left() + a.width() * 0.6, a.top());
        const QPointF c1 = start - QPointF(0, height);
        const QPointF c2 = end - QPointF(0, height);
        m_path.moveTo(start);
        m_path.cubicTo(c1, c2, end);
        m_arrow = arrowHead(end, end - c2);
        labelAt = m_path.pointAtPercent(0.5);
    }
    else {
        const QPointF d = b.center() - a.center();
        const double length = std::hypot(d.x(), d.y());
        const QPointF normal = qFuzzyIsNull(length) ? QPointF() : QPointF(d.y(), -d.x()) / length;
        const QPointF control = (a.center() + b.center()) / 2 + normal * m_bend;
        const QPointF start = exitPoint(a, control);
        const QPointF end = exitPoint(b, control);
        m_path.moveTo(start);
        m_path.quadTo(control, end);
        m_arrow = arrowHead(end, end - (qFuzzyIsNull(m_bend) ? start : control));
        labelAt = start * 0.25 + control * 0.5 + end * 0.25;
    }

    const QFontMetricsF metrics(m_flowScene.font());
    const QSizeF labelSize(metrics.horizontalAdvance(m_transitionId) + 2 * LABEL_PADDING,
                           metrics.height() + LABEL_PADDING);
    m_labelRect = QRectF(labelAt - QPointF(labelSize.width(), labelSize.height()) / 2, labelSize);
    update();
}

QRectF TransitionEdgeItem::boundingRect() const
{
    return m_path.boundingRect()
        .united(m_arrow.boundingRect())
        .united(m_labelRect)
        .adjusted(-PICK_WIDTH, -PICK_WIDTH, PICK_WIDTH, PICK_WIDTH);
}

QPainterPath TransitionEdgeItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(PICK_WIDTH);
    QPainterPath shape = stroker.createStroke(m_path);
    shape.addPolygon(m_arrow);
    shape.addRect(m_labelRect);
    return shape;
}

void TransitionEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                               QWidget* widget)
{
    Q_UNUSED(widget);
    if (m_path.isEmpty()) {
        return;
    }
    const QPalette& palette = option->palette;
    const bool selected = option->state & QStyle::State_Selected;
    const QColor color =
        selected ? palette.color(QPalette::Highlight) : palette.color(QPalette::WindowText);

    // `push` keeps the scene underneath, so it is dashed to read as "on top of".
    QPen pen(color, selected ? 2.0 : 1.5);
    pen.setStyle(m_mode == core::TransitionMode::Push ? Qt::DashLine : Qt::SolidLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_path);

    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(m_arrow);

    painter->setBrush(palette.color(QPalette::Window));
    painter->setPen(QPen(selected ? color : palette.color(QPalette::Mid), 1));
    painter->drawRoundedRect(m_labelRect, 3, 3);
    painter->setPen(palette.color(QPalette::WindowText));
    painter->setFont(m_flowScene.font());
    painter->drawText(m_labelRect, Qt::AlignCenter, m_transitionId);
}

}  // namespace shoal::editor
