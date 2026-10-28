#include "editor/EntityItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include "assets/SpriteSheet.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

using core::EntityType;

namespace {

constexpr double MARK_RADIUS = 8;
constexpr double CROSS_SIZE = 10;
constexpr double PLACEHOLDER_SIZE = 32;
constexpr double MIN_PICK_SIZE = 12;
const QColor MARK_COLOR(255, 160, 40);
const QColor REGION_COLOR(40, 180, 200);
const QColor CAMERA_COLOR(120, 200, 255);
const QColor PLACEHOLDER_COLOR(255, 0, 200);
constexpr double INACTIVE_OPACITY = 0.4;

QPointF pointOf(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return {array.at(0).toDouble(), array.at(1).toDouble()};
}

/// A region's shape, from `shape`: `rect` [x, y, w, h], `circle` [x, y, r] or `polygon`
/// [[x, y], ...]. Regions aren't modeled yet, so this reads what the file holds.
QPainterPath regionPath(const core::Entity& entity)
{
    const QJsonObject shape = entity.extras.value(QStringLiteral("shape")).toObject();
    QPainterPath path;
    if (const QJsonArray rect = shape.value(QStringLiteral("rect")).toArray(); rect.size() == 4) {
        path.addRect(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(),
                     rect[3].toDouble());
    }
    else if (const QJsonArray circle = shape.value(QStringLiteral("circle")).toArray();
             circle.size() == 3) {
        path.addEllipse(QPointF(circle[0].toDouble(), circle[1].toDouble()), circle[2].toDouble(),
                        circle[2].toDouble());
    }
    else if (const QJsonArray polygon = shape.value(QStringLiteral("polygon")).toArray();
             polygon.size() >= 2) {
        QPolygonF points;
        for (const QJsonValue& point : polygon) {
            points << pointOf(point);
        }
        path.addPolygon(points);
        path.closeSubpath();
    }
    return path;
}

QPainterPath linePath(const core::Entity& entity)
{
    QPainterPath path;
    const QJsonArray points = entity.extras.value(QStringLiteral("points")).toArray();
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (i == 0) {
            path.moveTo(pointOf(points[i]));
        }
        else {
            path.lineTo(pointOf(points[i]));
        }
    }
    if (entity.extras.value(QStringLiteral("closed")).toBool() && points.size() > 2) {
        path.closeSubpath();
    }
    return path;
}

QRectF boxRect(const core::Entity& entity)
{
    const QPointF size = pointOf(entity.extras.value(QStringLiteral("size")));
    return {0, 0, size.x(), size.y()};
}

QPen cosmeticPen(const QColor& color, double width = 1.5, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, width, style);
    pen.setCosmetic(true);
    return pen;
}

}  // namespace

EntityItem::EntityItem(SceneCanvas& canvas, core::EntityPath path, const core::Entity& entity,
                       QGraphicsItem* parent)
    : QGraphicsItem(parent), m_canvas(canvas), m_path(std::move(path))
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setEntity(entity);
}

void EntityItem::setEntity(const core::Entity& entity)
{
    m_entity = entity;
    m_entity.children.clear();
    const core::Transform& t = m_entity.transform;
    setPos(t.pos.x, t.pos.y);
    setTransform(QTransform::fromScale(t.scale.x, t.scale.y) * QTransform().rotate(t.rot));
    setOpacity(m_entity.active && m_entity.visible ? 1.0 : INACTIVE_OPACITY);
    refreshVisual();
}

void EntityItem::refreshVisual()
{
    prepareGeometryChange();
    m_hasImage = false;
    QRectF bounds;
    const bool flipX = m_entity.transform.flipX;
    const bool flipY = m_entity.transform.flipY;
    switch (m_entity.type) {
        case EntityType::Sprite:
        case EntityType::NineSlice:
        case EntityType::Animation: {
            SceneCanvas::SpriteVisual visual;
            if (m_entity.type == EntityType::Animation && m_entity.animation) {
                visual =
                    m_canvas.animationVisual(m_entity.animation->sheet, m_entity.animation->name);
            }
            else if (m_entity.sprite) {
                visual = m_canvas.spriteVisual(m_entity.sprite->sheet, m_entity.sprite->path);
            }
            if (visual.sprite) {
                m_hasImage = true;
                bounds = assets::placeSprite(*visual.sprite, visual.drawScale,
                                             flipX != visual.flipX, flipY != visual.flipY)
                             .bounds;
            }
            else {
                bounds = QRectF(-PLACEHOLDER_SIZE / 2, -PLACEHOLDER_SIZE / 2, PLACEHOLDER_SIZE,
                                PLACEHOLDER_SIZE);
            }
            break;
        }
        case EntityType::Mark:
            bounds = QRectF(-MARK_RADIUS, -MARK_RADIUS, 2 * MARK_RADIUS, 2 * MARK_RADIUS);
            break;
        case EntityType::Region:
            bounds = regionPath(m_entity).boundingRect();
            break;
        case EntityType::Path:
            bounds = linePath(m_entity).boundingRect();
            break;
        case EntityType::Rect:
        case EntityType::Text:
            bounds = boxRect(m_entity);
            break;
        case EntityType::Camera: {
            const double zoom = m_entity.extras.value(QStringLiteral("zoom")).toDouble(1);
            const QSizeF view = m_canvas.designResolution() / (zoom > 0 ? zoom : 1);
            bounds = QRectF(QPointF(-view.width() / 2, -view.height() / 2), view);
            break;
        }
        case EntityType::Empty:
        case EntityType::Group:
            break;
    }
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        // Nothing to draw, or a line: still something to click.
        bounds = bounds.united(
            QRectF(-MIN_PICK_SIZE / 2, -MIN_PICK_SIZE / 2, MIN_PICK_SIZE, MIN_PICK_SIZE));
    }
    if (!m_hasImage && (flipX || flipY)) {
        bounds = QTransform::fromScale(flipX ? -1 : 1, flipY ? -1 : 1).mapRect(bounds);
    }
    m_bounds = bounds;
    update();
}

QRectF EntityItem::boundingRect() const
{
    return m_bounds;
}

void EntityItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);
    const bool selected = option->state & QStyle::State_Selected;
    const bool image = m_entity.type == EntityType::Sprite ||
                       m_entity.type == EntityType::NineSlice ||
                       m_entity.type == EntityType::Animation;
    if (image) {
        paintSprite(painter, selected);
    }
    else {
        painter->save();
        painter->scale(m_entity.transform.flipX ? -1 : 1, m_entity.transform.flipY ? -1 : 1);
        paintShape(painter);
        painter->restore();
    }
    if (selected) {
        painter->setPen(cosmeticPen(option->palette.color(QPalette::Highlight), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_bounds);
    }
}

void EntityItem::paintSprite(QPainter* painter, bool selected)
{
    Q_UNUSED(selected);
    SceneCanvas::SpriteVisual visual;
    if (m_entity.type == EntityType::Animation && m_entity.animation) {
        visual = m_canvas.animationVisual(m_entity.animation->sheet, m_entity.animation->name);
    }
    else if (m_entity.sprite) {
        visual = m_canvas.spriteVisual(m_entity.sprite->sheet, m_entity.sprite->path);
    }
    if (!visual.sprite || visual.atlas.isNull()) {
        // Missing sheet, sprite or atlas: a box the size the sprite would have, if known.
        painter->setPen(cosmeticPen(PLACEHOLDER_COLOR));
        painter->setBrush(QColor(255, 0, 200, 40));
        painter->drawRect(m_bounds);
        painter->drawLine(m_bounds.topLeft(), m_bounds.bottomRight());
        painter->drawLine(m_bounds.topRight(), m_bounds.bottomLeft());
        return;
    }
    const assets::SpritePlacement placement = assets::placeSprite(
        *visual.sprite, visual.drawScale, m_entity.transform.flipX != visual.flipX,
        m_entity.transform.flipY != visual.flipY);
    painter->save();
    painter->setTransform(placement.transform, true);
    painter->drawPixmap(placement.target, visual.atlas, placement.source);
    painter->restore();
}

void EntityItem::paintShape(QPainter* painter)
{
    const QColor fallback = m_entity.type == EntityType::Mark     ? MARK_COLOR
                            : m_entity.type == EntityType::Camera ? CAMERA_COLOR
                                                                  : REGION_COLOR;
    const QColor color = m_canvas.classColor(m_entity.classes, fallback);
    QColor fill = color;
    fill.setAlpha(m_entity.type == EntityType::Region ? 24 : 50);

    switch (m_entity.type) {
        case EntityType::Mark:
            painter->setPen(cosmeticPen(color, 2));
            painter->setBrush(fill);
            painter->drawEllipse(QPointF(), MARK_RADIUS, MARK_RADIUS);
            painter->drawLine(QPointF(-MARK_RADIUS, 0), QPointF(MARK_RADIUS, 0));
            painter->drawLine(QPointF(0, -MARK_RADIUS), QPointF(0, MARK_RADIUS));
            break;
        case EntityType::Region:
            painter->setPen(cosmeticPen(color));
            painter->setBrush(fill);
            painter->drawPath(regionPath(m_entity));
            break;
        case EntityType::Path:
            painter->setPen(cosmeticPen(color, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(linePath(m_entity));
            break;
        case EntityType::Camera:
            painter->setPen(cosmeticPen(color, 1.5, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_bounds);
            painter->drawLine(QPointF(-CROSS_SIZE, 0), QPointF(CROSS_SIZE, 0));
            painter->drawLine(QPointF(0, -CROSS_SIZE), QPointF(0, CROSS_SIZE));
            break;
        case EntityType::Rect:
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(m_entity.extras.value(QStringLiteral("color")).toString()));
            painter->drawRect(boxRect(m_entity));
            break;
        case EntityType::Text:
            painter->setPen(cosmeticPen(color, 1, Qt::DotLine));
            painter->drawRect(boxRect(m_entity));
            painter->drawText(boxRect(m_entity), Qt::AlignCenter | Qt::TextWordWrap,
                              m_entity.extras.value(QStringLiteral("text")).toString());
            break;
        case EntityType::Empty:
        case EntityType::Group:
            painter->setPen(cosmeticPen(color, 1));
            painter->drawLine(QPointF(-CROSS_SIZE, 0), QPointF(CROSS_SIZE, 0));
            painter->drawLine(QPointF(0, -CROSS_SIZE), QPointF(0, CROSS_SIZE));
            break;
        case EntityType::Sprite:
        case EntityType::NineSlice:
        case EntityType::Animation:
            break;
    }
}

QVariant EntityItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && m_canvas.isDragging() &&
        !(QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
        return m_canvas.snap(value.toPointF());  // Alt drags freely
    }
    return QGraphicsItem::itemChange(change, value);
}

void EntityItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_canvas.setDragging(true);
    }
    QGraphicsItem::mousePressEvent(event);
}

void EntityItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) {
        m_canvas.setDragging(false);
        m_canvas.commitMoves();
    }
}

}  // namespace shoal::editor
