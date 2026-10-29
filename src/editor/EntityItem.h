#pragma once

#include <QGraphicsItem>
#include <optional>

#include "assets/SpriteSheet.h"
#include "core/Model.h"
#include "core/SceneTree.h"

namespace shoal::editor {

class SceneCanvas;

/// An entity on the scene canvas, drawn from its parts: its fill, its image in its rect, its
/// text, and outlines for what only the editor shows (views, regions, shapes, paths,
/// marks). Its position, rotation and scale are the entity's transform, measured from its
/// alignment point in its parent's item, so children follow their parent. Flips mirror only
/// the entity's own drawing, as in sprat, not its children. A tiled container's grid is
/// the canvas's to draw (see TileGridOverlay).
class EntityItem : public QGraphicsItem {
public:
    enum { Type = UserType + 10 };

    /// What the entity's place in the tree adds to its own fields.
    struct Placement {
        QPointF offset;       ///< Its anchor point in its parent (see core::anchorPoint()).
        core::Color tint;     ///< Its effective tint (see core::effectiveTint()).
        bool screen = false;  ///< In screen space, which isn't drawn yet.
        /// What its parent offers it to measure against: on an axis in tiles, a drag moves
        /// it from tile to tile.
        core::Frame frame;
    };

    EntityItem(SceneCanvas& canvas, core::EntityPath path, const core::Entity& entity,
               const Placement& placement, QGraphicsItem* parent);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    const core::EntityPath& path() const { return m_path; }
    const core::Entity& entity() const { return m_entity; }
    const Placement& placement() const { return m_placement; }
    /// What the entity is (see core::kindOf()).
    const QString& kind() const { return m_kind; }
    /// What the entity draws, in item coordinates: picking, outlines and the gizmos.
    QRectF visualRect() const { return m_bounds; }
    /// Its rect, placed by its pivot; null when it has none.
    std::optional<QRectF> rect() const { return m_rect; }
    /// Its own grid, as a tiled container, in item coordinates: flips don't mirror it, as
    /// it places the children.
    const std::optional<core::TileGrid>& tileGrid() const { return m_grid; }
    /// What it offers its children to measure against (see core::childFrame()).
    core::Frame childFrame() const { return {m_coreRect, m_grid}; }
    /// Whether only the editor draws it: it has nothing the game draws, so it goes under
    /// what does.
    bool isGuide() const;
    /// The position its item's position stands for: the item's, less its anchor point.
    core::Point entityPosition() const;
    /// Takes the entity's own fields; its children are items of their own.
    void setEntity(const core::Entity& entity, const Placement& placement);
    void setEntity(const core::Entity& entity) { setEntity(entity, m_placement); }
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
    void paintImage(QPainter* painter);
    void paintGuide(QPainter* painter);
    QPainterPath guidePath() const;

    SceneCanvas& m_canvas;
    core::EntityPath m_path;
    core::Entity m_entity;  ///< Without its children, which are items of their own.
    Placement m_placement;
    QString m_kind;  ///< See core::kindOf().
    QRectF m_bounds;
    std::optional<core::TileGrid> m_grid;  ///< Its own, as a tiled container.
    std::optional<QRectF> m_rect;
    std::optional<core::RectF> m_coreRect;  ///< The same, for its children's frame.
    bool m_hasImage = false;                ///< Whether its sprite was found.
    bool m_moving = false;                  ///< Whether the press became a drag.
    assets::NineSlicePlacement m_image;     ///< Its cells, when it has an image.
};

}  // namespace shoal::editor
