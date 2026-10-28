#pragma once

#include <QGraphicsItem>

#include "assets/SpriteSheet.h"
#include "core/Model.h"
#include "core/SceneTree.h"

namespace shoal::editor {

class SceneCanvas;

/// An entity on the scene canvas. Its position, rotation and scale are the entity's
/// transform, relative to its parent's item, so children follow their parent. Flips mirror
/// only the entity's own drawing, as in sprat, not its children.
class EntityItem : public QGraphicsItem {
public:
    enum { Type = UserType + 10 };

    EntityItem(SceneCanvas& canvas, core::EntityPath path, const core::Entity& entity,
               QGraphicsItem* parent);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    const core::EntityPath& path() const { return m_path; }
    const core::Entity& entity() const { return m_entity; }
    /// What the entity draws, in item coordinates: picking, outlines and the gizmos.
    QRectF visualRect() const { return m_bounds; }
    /// A nine-slice's size: its `size`, or its sprite's natural size if it has none. Empty
    /// for other entities, or when the sprite can't be found.
    QSizeF nineSliceSize() const { return m_nineSliceSize; }
    /// Takes the entity's own fields; its children are items of their own.
    void setEntity(const core::Entity& entity);
    /// Recomputes what it draws, after a sheet changed.
    void refreshVisual();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    /// Moves the selection once the mouse has moved past the drag distance, so a click
    /// never moves it, not even when the view scrolls under the pressed button.
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void paintSprite(QPainter* painter, bool selected);
    void paintShape(QPainter* painter);

    SceneCanvas& m_canvas;
    core::EntityPath m_path;
    core::Entity m_entity;  ///< Without its children, which are items of their own.
    QRectF m_bounds;
    bool m_hasImage = false;
    bool m_moving = false;  ///< Whether the press became a drag.
    QSizeF m_nineSliceSize;
    assets::NineSlicePlacement m_nineSlice;  ///< Its cells, when it draws as a nine-slice.
};

}  // namespace shoal::editor
