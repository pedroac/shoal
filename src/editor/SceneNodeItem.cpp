#include "editor/SceneNodeItem.h"

#include <QCoreApplication>
#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include "editor/FlowScene.h"

namespace shoal::editor {

namespace {
constexpr double TITLE_HEIGHT = 32;
constexpr double LINE_HEIGHT = 18;
constexpr double PADDING = 10;
constexpr double PORT_RADIUS = 6;
constexpr double CORNER_RADIUS = 6;
constexpr double MARGIN = 3;  ///< Room for the selection outline.
}  // namespace

SceneNodeItem::SceneNodeItem(FlowScene& flowScene, QString sceneId)
    : m_flowScene(flowScene), m_sceneId(std::move(sceneId))
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setZValue(1);  // above the edges
    relayout();
}

QRectF SceneNodeItem::bodyRect() const
{
    return {0, 0, WIDTH, m_height};
}

QRectF SceneNodeItem::portRect() const
{
    return {WIDTH - PORT_RADIUS, TITLE_HEIGHT / 2 - PORT_RADIUS, 2 * PORT_RADIUS, 2 * PORT_RADIUS};
}

QRectF SceneNodeItem::boundingRect() const
{
    return bodyRect().united(portRect()).adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN);
}

void SceneNodeItem::setSceneId(const QString& sceneId)
{
    m_sceneId = sceneId;
    update();
}

void SceneNodeItem::setStart(bool start)
{
    if (m_start != start) {
        m_start = start;
        update();
    }
}

void SceneNodeItem::setModifierCount(int count)
{
    if (m_modifierCount != count) {
        m_modifierCount = count;
        relayout();
    }
}

void SceneNodeItem::setExits(const QStringList& exits, const QStringList& transitionIds)
{
    m_exitIds = transitionIds;
    if (m_exits != exits) {
        m_exits = exits;
        relayout();
    }
    update();
}

QRectF SceneNodeItem::exitRect(const QString& transitionId) const
{
    const qsizetype index = m_exitIds.indexOf(transitionId);
    if (index < 0) {
        return {};
    }
    const int line = int(index) + (m_modifierCount > 0 ? 1 : 0);
    return {PADDING / 2, TITLE_HEIGHT + line * LINE_HEIGHT, WIDTH - PADDING, LINE_HEIGHT};
}

void SceneNodeItem::relayout()
{
    const int lines = int(m_exits.size()) + (m_modifierCount > 0 ? 1 : 0);
    const double height = TITLE_HEIGHT + (lines > 0 ? lines * LINE_HEIGHT + PADDING / 2 : 0);
    if (height != m_height) {
        prepareGeometryChange();
        m_height = height;
        m_flowScene.handleNodeMoved(*this);  // edges attach to the node's outline
    }
    update();
}

void SceneNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                          QWidget* widget)
{
    Q_UNUSED(widget);
    const QPalette& palette = option->palette;
    const bool selected = option->state & QStyle::State_Selected;
    const QFont baseFont = painter->font();

    QPen border(selected ? palette.color(QPalette::Highlight) : palette.color(QPalette::Mid));
    border.setWidthF(selected || m_start ? 2 : 1);
    painter->setPen(border);
    painter->setBrush(palette.color(QPalette::Base));
    painter->drawRoundedRect(bodyRect(), CORNER_RADIUS, CORNER_RADIUS);

    if (m_start) {
        // A strip along the left edge marks where the game begins.
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.color(QPalette::Highlight));
        painter->drawRoundedRect(QRectF(0, 0, 6, m_height), 3, 3);
    }

    QFont titleFont = painter->font();
    titleFont.setBold(true);
    const QRectF titleRect(PADDING + (m_start ? 4 : 0), 0, WIDTH - 2 * PADDING - PORT_RADIUS,
                           TITLE_HEIGHT);
    painter->setFont(titleFont);
    painter->setPen(palette.color(QPalette::Text));
    painter->drawText(
        titleRect, Qt::AlignVCenter | Qt::AlignLeft,
        QFontMetricsF(titleFont).elidedText(m_sceneId, Qt::ElideRight, titleRect.width()));

    painter->setFont(baseFont);
    painter->setPen(palette.color(QPalette::PlaceholderText));
    QRectF lineRect(PADDING, TITLE_HEIGHT, WIDTH - 2 * PADDING, LINE_HEIGHT);
    QStringList lines;
    if (m_modifierCount > 0) {
        lines << (m_modifierCount == 1
                      ? QCoreApplication::translate("shoal::editor::SceneNodeItem", "1 modifier")
                      : QCoreApplication::translate("shoal::editor::SceneNodeItem", "%n modifiers",
                                                    nullptr, m_modifierCount));
    }
    const QFontMetricsF metrics(baseFont);
    for (const QString& line : std::as_const(lines)) {
        painter->drawText(lineRect, Qt::AlignVCenter | Qt::AlignLeft,
                          metrics.elidedText(line, Qt::ElideRight, lineRect.width()));
        lineRect.translate(0, LINE_HEIGHT);
    }
    // An exit's transition is selected on its line.
    for (qsizetype i = 0; i < m_exits.size(); ++i) {
        const bool chosen = m_flowScene.selectedExit() == m_exitIds.value(i);
        if (chosen) {
            const QRectF rect = exitRect(m_exitIds[i]);
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.color(QPalette::Highlight));
            painter->drawRoundedRect(rect, 3, 3);
            painter->setPen(palette.color(QPalette::HighlightedText));
        }
        else {
            painter->setPen(palette.color(QPalette::PlaceholderText));
        }
        painter->drawText(lineRect, Qt::AlignVCenter | Qt::AlignLeft,
                          metrics.elidedText(m_exits[i], Qt::ElideRight, lineRect.width()));
        lineRect.translate(0, LINE_HEIGHT);
    }

    painter->setPen(QPen(palette.color(QPalette::Mid), 1));
    painter->setBrush(palette.color(QPalette::Button));
    painter->drawEllipse(portRect());
}

QVariant SceneNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) {
        m_flowScene.handleNodeMoved(*this);
    }
    return QGraphicsItem::itemChange(change, value);
}

void SceneNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && portRect().contains(event->pos())) {
        m_connecting = true;
        m_flowScene.beginConnection(*this, event->scenePos());
        event->accept();
        return;
    }
    for (const QString& id : std::as_const(m_exitIds)) {
        if (event->button() == Qt::LeftButton && exitRect(id).contains(event->pos())) {
            m_flowScene.select({}, {id});
            m_exitPressed = true;
            event->accept();
            return;
        }
    }
    QGraphicsItem::mousePressEvent(event);
}

void SceneNodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_exitPressed) {
        return;  // a click on a line doesn't drag the node
    }
    if (m_connecting) {
        m_flowScene.updateConnection(event->scenePos());
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void SceneNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_connecting) {
        m_connecting = false;
        m_flowScene.finishConnection(event->scenePos());
        return;
    }
    if (m_exitPressed) {
        m_exitPressed = false;  // and the node stays unselected
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) {
        m_flowScene.commitNodeMoves();
    }
}

void SceneNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !portRect().contains(event->pos())) {
        emit m_flowScene.sceneActivated(m_sceneId);
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void SceneNodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    setCursor(portRect().contains(event->pos()) ? Qt::CrossCursor : Qt::ArrowCursor);
    QGraphicsItem::hoverMoveEvent(event);
}

}  // namespace shoal::editor
