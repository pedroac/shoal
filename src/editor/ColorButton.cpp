#include "editor/ColorButton.h"

#include <QColorDialog>
#include <QPainter>
#include <QPixmap>

namespace shoal::editor {

namespace {
constexpr int SWATCH_WIDTH = 32;
constexpr int SWATCH_HEIGHT = 14;
constexpr int CHECKER = 4;
}  // namespace

ColorButton::ColorButton(QWidget* parent) : QToolButton(parent)
{
    connect(this, &QToolButton::clicked, this, &ColorButton::choose);
    setColor(m_color);
}

void ColorButton::setColor(const core::Color& color)
{
    m_color = color;
    // A checkerboard under the color, so its alpha shows.
    QPixmap swatch(SWATCH_WIDTH, SWATCH_HEIGHT);
    swatch.fill(Qt::white);
    QPainter painter(&swatch);
    for (int y = 0; y < SWATCH_HEIGHT; y += CHECKER) {
        for (int x = (y / CHECKER % 2) * CHECKER; x < SWATCH_WIDTH; x += 2 * CHECKER) {
            painter.fillRect(x, y, CHECKER, CHECKER, Qt::lightGray);
        }
    }
    painter.fillRect(swatch.rect(), QColor(color.red, color.green, color.blue, color.alpha));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    setIcon(swatch);
    setIconSize(swatch.size());
    setToolTip(color.name());
}

void ColorButton::choose()
{
    const QColor chosen =
        QColorDialog::getColor(QColor(m_color.red, m_color.green, m_color.blue, m_color.alpha),
                               this, QString(), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) {
        return;
    }
    const core::Color color{quint8(chosen.red()), quint8(chosen.green()), quint8(chosen.blue()),
                            quint8(chosen.alpha())};
    if (color != m_color) {
        setColor(color);
        emit colorChosen(color);
    }
}

}  // namespace shoal::editor
