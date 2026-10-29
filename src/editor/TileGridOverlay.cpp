#include "editor/TileGridOverlay.h"

#include <QPainter>

#include "editor/EntityItem.h"

namespace shoal::editor {

namespace {
const QColor GRID_LIGHT(255, 255, 255, 170);
const QColor GRID_DARK(0, 0, 0, 200);

QPen cosmeticPen(const QColor& color, Qt::PenStyle style)
{
    QPen pen(color, 1, style);
    pen.setCosmetic(true);
    return pen;
}
}  // namespace

TileGridOverlay::TileGridOverlay()
{
    setZValue(1e12 - 1);  // above every layer, under the handles
    setAcceptedMouseButtons(Qt::NoButton);
}

void TileGridOverlay::showGrids(const QList<const EntityItem*>& containers)
{
    QList<Shown> grids;
    QRectF bounds;
    for (const EntityItem* container : containers) {
        const std::optional<core::TileGrid>& grid = container->tileGrid();
        if (!grid || !container->isVisible()) {
            continue;
        }
        Shown shown{container->sceneTransform(),
                    QRectF(grid->origin.x, grid->origin.y, grid->columns * grid->tile.width,
                           grid->rows * grid->tile.height),
                    grid->tile.width,
                    grid->tile.height,
                    grid->columns,
                    grid->rows};
        bounds = bounds.united(shown.transform.mapRect(shown.area).adjusted(-1, -1, 1, 1));
        grids << shown;
    }
    if (grids.isEmpty() && m_grids.isEmpty()) {
        return;
    }
    prepareGeometryChange();
    m_grids = std::move(grids);
    m_bounds = bounds;
    update();
}

void TileGridOverlay::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                            QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    for (const Shown& grid : std::as_const(m_grids)) {
        QVector<QLineF> lines;
        for (int column = 0; column <= grid.columns; ++column) {
            const double x = grid.area.left() + column * grid.tileWidth;
            lines << QLineF(x, grid.area.top(), x, grid.area.bottom());
        }
        for (int row = 0; row <= grid.rows; ++row) {
            const double y = grid.area.top() + row * grid.tileHeight;
            lines << QLineF(grid.area.left(), y, grid.area.right(), y);
        }
        painter->save();
        painter->setTransform(grid.transform, true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(cosmeticPen(GRID_LIGHT, Qt::SolidLine));
        painter->drawLines(lines);
        painter->setPen(cosmeticPen(GRID_DARK, Qt::DotLine));
        painter->drawLines(lines);
        painter->restore();
    }
}

}  // namespace shoal::editor
