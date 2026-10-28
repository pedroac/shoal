#pragma once

#include <QGraphicsItem>

#include "core/SceneTree.h"

namespace shoal::editor {

class SceneCanvas;

/// A handle on the one selected entity: drag it to rotate the entity about its origin, or
/// to scale it. A nine-slice is resized instead, so its borders keep their size: the
/// handle's corner follows the mouse on each axis. It keeps its size on screen whatever the zoom.
/// Shift snaps rotation to 15 degrees; the result is one undo step when the mouse is released.
class TransformHandle : public QGraphicsItem {
public:
    enum { Type = UserType + 11 };
    enum class Kind { Rotate, Scale };

    TransformHandle(SceneCanvas& canvas, Kind kind);

    int type() const override { return Type; }
    Kind kind() const { return m_kind; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /// Places the handle on `target`'s visual outline, or hides it for an empty path.
    void attach(const core::EntityPath& target);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    SceneCanvas& m_canvas;
    Kind m_kind;
    core::EntityPath m_target;
    QPointF m_origin;  ///< The entity's origin, in scene coordinates, when the drag began.
    double m_startAngle = 0;
    double m_startDistance = 0;
    double m_rotation = 0;
    core::Point m_scale;
    bool m_resize = false;  ///< Resizing a nine-slice rather than scaling.
    QSizeF m_startSize;
    QSizeF m_size;
    QPointF m_side;  ///< The handle's corner, as a fraction of the size from the origin.
};

}  // namespace shoal::editor
