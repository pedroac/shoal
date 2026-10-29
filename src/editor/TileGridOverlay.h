#pragma once

#include <QGraphicsItem>
#include <QList>

class QPainter;

namespace shoal::editor {

class EntityItem;

/// The grids of some tiled containers, drawn above every entity so their children don't
/// cover them: a dotted line, dark on light, that reads on any content. It takes no clicks.
class TileGridOverlay : public QGraphicsItem {
public:
    enum { Type = UserType + 12 };

    TileGridOverlay();

    int type() const override { return Type; }
    QRectF boundingRect() const override { return m_bounds; }
    QPainterPath shape() const override { return {}; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /// Shows the grids of `containers`, where they are now; the items without a grid are
    /// skipped.
    void showGrids(const QList<const EntityItem*>& containers);

private:
    struct Shown {
        QTransform transform;  ///< From the container's item to the scene.
        QRectF area;
        double tileWidth = 0;
        double tileHeight = 0;
        int columns = 0;
        int rows = 0;
    };
    QList<Shown> m_grids;
    QRectF m_bounds;
};

}  // namespace shoal::editor
