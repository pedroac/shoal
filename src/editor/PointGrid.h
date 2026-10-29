#pragma once

#include <QWidget>
#include <optional>

#include "core/Model.h"

class QToolButton;

namespace shoal::editor {

/// Nine points of a rect, as fractions of it, from the top-left `[0, 0]` to the
/// bottom-right `[1, 1]`: what the inspector picks an alignment or a pivot with. The point
/// shown is checked; one that isn't on the grid checks nothing.
class PointGrid : public QWidget {
    Q_OBJECT
public:
    explicit PointGrid(QWidget* parent = nullptr);

    void setPoint(std::optional<core::Point> point);
    /// The button of a point of the grid, `column` and `row` from 0 to 2.
    QToolButton* button(int column, int row) const;

signals:
    /// A point was clicked, with the keyboard modifiers held.
    void pointChosen(core::Point point, Qt::KeyboardModifiers modifiers);

private:
    QToolButton* m_buttons[3][3];
};

}  // namespace shoal::editor
