#include "editor/PointGrid.h"

#include <QApplication>
#include <QGridLayout>
#include <QToolButton>

namespace shoal::editor {

namespace {
constexpr int BUTTON_SIZE = 14;
}

PointGrid::PointGrid(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins({});
    layout->setSpacing(1);
    const QString names[3][3] = {
        {tr("Top left"), tr("Left"), tr("Bottom left")},
        {tr("Top"), tr("Center"), tr("Bottom")},
        {tr("Top right"), tr("Right"), tr("Bottom right")},
    };
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            auto* button = new QToolButton(this);
            button->setCheckable(true);
            button->setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
            button->setToolTip(names[column][row]);
            const core::Point point{column / 2.0, row / 2.0};
            connect(button, &QToolButton::clicked, this,
                    [this, point] { emit pointChosen(point, QApplication::keyboardModifiers()); });
            layout->addWidget(button, row, column);
            m_buttons[column][row] = button;
        }
    }
}

void PointGrid::setPoint(std::optional<core::Point> point)
{
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            m_buttons[column][row]->setChecked(point &&
                                               *point == core::Point{column / 2.0, row / 2.0});
        }
    }
}

QToolButton* PointGrid::button(int column, int row) const
{
    return m_buttons[column][row];
}

}  // namespace shoal::editor
