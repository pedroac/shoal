#include "editor/EntityItem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include "assets/SpriteSheet.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

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

QPen cosmeticPen(const QColor& color, double width = 1.5, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, width, style);
    pen.setCosmetic(true);
    return pen;
}

QColor qColor(const core::Color& color)
{
    return QColor(color.red, color.green, color.blue, color.alpha);
}

/// `color` multiplied by a tint, as the game draws it.
QColor tinted(const core::Color& color, const core::Color& tint)
{
    return qColor(color.multiplied(tint));
}

QString textOf(const core::Entity& entity)
{
    return entity.extras.value(QStringLiteral("text")).toString();
}

}  // namespace

EntityItem::EntityItem(SceneCanvas& canvas, core::EntityPath path, const core::Entity& entity,
                       const Placement& placement, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_canvas(canvas), m_path(std::move(path))
{
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setEntity(entity, placement);
}

void EntityItem::setEntity(const core::Entity& entity, const Placement& placement)
{
    m_entity = entity;
    m_kind = core::kindOf(entity);  // a group is one by its children
    m_entity.children.clear();
    m_placement = placement;
    const core::Transform& t = m_entity.transform;
    setPos(t.pos.x + placement.offset.x(), t.pos.y + placement.offset.y());
    setTransform(QTransform::fromScale(t.scale.x, t.scale.y) * QTransform().rotate(t.rot));
    setOpacity(m_entity.active && m_entity.visible ? 1.0 : INACTIVE_OPACITY);
    setFlag(ItemClipsChildrenToShape, m_entity.clip);
    setFlag(ItemClipsToShape, m_entity.clip);
    refreshVisual();
}

core::Point EntityItem::entityPosition() const
{
    return {pos().x() - m_placement.offset.x(), pos().y() - m_placement.offset.y()};
}

bool EntityItem::isGuide() const
{
    return !m_entity.image && !m_entity.fill && textOf(m_entity).isEmpty();
}

void EntityItem::refreshVisual()
{
    prepareGeometryChange();
    m_hasImage = false;
    m_image = {};
    const core::ImageMetricsFn metrics = m_canvas.imageMetrics();
    const std::optional<core::RectF> rect = core::entityRect(m_entity, m_placement.frame, metrics);
    m_coreRect = rect;
    m_rect =
        rect ? std::optional(QRectF(rect->x, rect->y, rect->width, rect->height)) : std::nullopt;
    const bool flipX = m_entity.transform.flipX;
    const bool flipY = m_entity.transform.flipY;
    const QTransform mirror = QTransform::fromScale(flipX ? -1 : 1, flipY ? -1 : 1);

    QRectF bounds;
    if (m_entity.image) {
        const SceneCanvas::SpriteVisual visual = m_canvas.visualOf(*m_entity.image);
        if (visual.sprite && m_rect) {
            m_hasImage = true;
            m_image = assets::placeInBox(*visual.sprite, visual.drawScale, *m_rect,
                                         m_entity.image->mode == core::ImageMode::NineSlice,
                                         flipX != visual.flipX, flipY != visual.flipY);
            // What it draws, for picking: the trimmed content, not the whole rect.
            for (const assets::SpritePlacement& cell : m_image.cells) {
                bounds = bounds.united(cell.bounds);
            }
        }
        else {
            // Missing sheet or sprite: a box where it would be, the size it would have.
            bounds = mirror.mapRect(m_rect ? *m_rect
                                           : QRectF(-PLACEHOLDER_SIZE / 2, -PLACEHOLDER_SIZE / 2,
                                                    PLACEHOLDER_SIZE, PLACEHOLDER_SIZE));
        }
    }
    if (m_rect && (m_entity.fill || !textOf(m_entity).isEmpty())) {
        bounds = bounds.united(mirror.mapRect(*m_rect));
    }
    if (isGuide()) {
        bounds = bounds.united(mirror.mapRect(guidePath().boundingRect()));
        if (m_kind != QStringLiteral("group") && (bounds.width() <= 0 || bounds.height() <= 0)) {
            // A point or a line: still something to click.
            bounds = bounds.united(
                QRectF(-MIN_PICK_SIZE / 2, -MIN_PICK_SIZE / 2, MIN_PICK_SIZE, MIN_PICK_SIZE));
        }
    }
    m_bounds = bounds;
    m_grid = core::tileGrid(m_entity, m_placement.frame, metrics);
    update();
}

QRectF EntityItem::boundingRect() const
{
    return m_bounds;
}

QPainterPath EntityItem::shape() const
{
    QPainterPath path;
    if (m_entity.clip && m_rect) {
        path.addRect(QTransform::fromScale(m_entity.transform.flipX ? -1 : 1,
                                           m_entity.transform.flipY ? -1 : 1)
                         .mapRect(*m_rect));
    }
    else {
        path.addRect(m_bounds);
    }
    return path;
}

void EntityItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);
    const core::Color& tint = m_placement.tint;
    painter->save();
    painter->scale(m_entity.transform.flipX ? -1 : 1, m_entity.transform.flipY ? -1 : 1);
    if (m_entity.fill && m_rect) {
        painter->fillRect(*m_rect, tinted(*m_entity.fill, tint));
    }
    painter->restore();
    if (m_entity.image) {
        paintImage(painter);
    }
    painter->save();
    painter->scale(m_entity.transform.flipX ? -1 : 1, m_entity.transform.flipY ? -1 : 1);
    if (const QString text = textOf(m_entity); !text.isEmpty() && m_rect) {
        painter->setPen(tinted(core::Color{}, tint));
        painter->drawText(*m_rect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }
    if (isGuide()) {
        paintGuide(painter);
    }
    painter->restore();
    if (option->state & QStyle::State_Selected) {
        painter->setPen(cosmeticPen(option->palette.color(QPalette::Highlight), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_bounds);
    }
}

void EntityItem::paintImage(QPainter* painter)
{
    const SceneCanvas::SpriteVisual visual = m_canvas.visualOf(*m_entity.image);
    if (!m_hasImage || visual.atlas.isNull()) {
        // Missing sheet, sprite or atlas: a box the size the sprite would have, if known.
        painter->setPen(cosmeticPen(PLACEHOLDER_COLOR));
        painter->setBrush(QColor(255, 0, 200, 40));
        painter->drawRect(m_bounds);
        painter->drawLine(m_bounds.topLeft(), m_bounds.bottomRight());
        painter->drawLine(m_bounds.topRight(), m_bounds.bottomLeft());
        return;
    }
    const core::Color& tint = m_placement.tint;
    const QPixmap atlas = m_canvas.tintedAtlas(visual, tint);
    painter->save();
    painter->setOpacity(painter->opacity() * tint.alpha / 255.0);
    for (const assets::SpritePlacement& cell : m_image.cells) {
        painter->save();
        painter->setTransform(cell.transform, true);
        painter->drawPixmap(cell.target, atlas, cell.source);
        painter->restore();
    }
    painter->restore();
}

QPainterPath EntityItem::guidePath() const
{
    QPainterPath path;
    const QJsonObject& extras = m_entity.extras;
    const QJsonObject shape = extras.value(QStringLiteral("shape")).toObject();
    const QJsonObject line = extras.value(QStringLiteral("path")).toObject();
    if (extras.contains(QStringLiteral("view"))) {
        const QSizeF view = m_canvas.designResolution();
        path.addRect(m_rect.value_or(QRectF(QPointF(-view.width() / 2, -view.height() / 2), view)));
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
    else if (shape.value(QStringLiteral("circle")).isDouble()) {
        const double radius = shape.value(QStringLiteral("circle")).toDouble();
        path.addEllipse(QPointF(), radius, radius);
    }
    else if (const QJsonArray points = line.value(QStringLiteral("points")).toArray();
             !points.isEmpty()) {
        for (qsizetype i = 0; i < points.size(); ++i) {
            if (i == 0) {
                path.moveTo(pointOf(points[i]));
            }
            else {
                path.lineTo(pointOf(points[i]));
            }
        }
        if (line.value(QStringLiteral("closed")).toBool() && points.size() > 2) {
            path.closeSubpath();
        }
    }
    else if (m_rect) {
        path.addRect(*m_rect);
    }
    return path;
}

void EntityItem::paintGuide(QPainter* painter)
{
    const QString& kind = m_kind;
    const QColor fallback = kind == QStringLiteral("mark")     ? MARK_COLOR
                            : kind == QStringLiteral("camera") ? CAMERA_COLOR
                                                               : REGION_COLOR;
    const QColor color = m_canvas.classColor(m_entity.classes, fallback);
    QColor fill = color;
    fill.setAlpha(24);

    if (kind == QStringLiteral("group")) {
        return;  // drawn as its children
    }
    if (kind == QStringLiteral("mark")) {
        fill.setAlpha(50);
        painter->setPen(cosmeticPen(color, 2));
        painter->setBrush(fill);
        painter->drawEllipse(QPointF(), MARK_RADIUS, MARK_RADIUS);
        painter->drawLine(QPointF(-MARK_RADIUS, 0), QPointF(MARK_RADIUS, 0));
        painter->drawLine(QPointF(0, -MARK_RADIUS), QPointF(0, MARK_RADIUS));
        return;
    }
    if (kind == QStringLiteral("camera")) {
        painter->setPen(cosmeticPen(color, 1.5, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(guidePath());
        painter->drawLine(QPointF(-CROSS_SIZE, 0), QPointF(CROSS_SIZE, 0));
        painter->drawLine(QPointF(0, -CROSS_SIZE), QPointF(0, CROSS_SIZE));
        return;
    }
    if (kind == QStringLiteral("path")) {
        painter->setPen(cosmeticPen(color, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(guidePath());
        return;
    }
    painter->setPen(cosmeticPen(color));
    painter->setBrush(fill);
    painter->drawPath(guidePath());
}

QVariant EntityItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged && isSelected()) {
        m_canvas.updateHandles();  // they follow a drag
    }
    if (change == ItemPositionChange && m_canvas.isDragging() &&
        !(QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {  // Alt drags freely
        // Its position snaps, measured from its anchor point, and on an axis in tiles it
        // goes from tile to tile, keeping its offset.
        const QPointF offset = m_placement.offset;
        const QPointF snapped = m_canvas.snap(value.toPointF() - offset) + offset;
        const core::Entity moved =
            core::movedTo(m_placement.frame, m_entity, {snapped.x(), snapped.y()});
        const core::Point anchor = core::anchorPoint(m_placement.frame, moved.at);
        return QPointF(anchor.x + moved.transform.pos.x, anchor.y + moved.transform.pos.y);
    }
    return QGraphicsItem::itemChange(change, value);
}

void EntityItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_canvas.setDragging(true);
    }
    m_moving = false;
    QGraphicsItem::mousePressEvent(event);
}

void EntityItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // A view that scrolls, as when a panel's layout resizes it, replays the last mouse
    // event: the cursor stays still on screen but moves in the scene.
    if (!m_moving) {
        const QPoint moved = event->screenPos() - event->buttonDownScreenPos(Qt::LeftButton);
        if (moved.manhattanLength() < QApplication::startDragDistance()) {
            return;
        }
        m_moving = true;
    }
    QGraphicsItem::mouseMoveEvent(event);
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
