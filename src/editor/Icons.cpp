#include "editor/Icons.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QSvgRenderer>

namespace shoal::editor {

namespace {

QString fileOf(Icon which)
{
    switch (which) {
        case Icon::Add:
            return QStringLiteral(":/icons/add.svg");
        case Icon::Remove:
            return QStringLiteral(":/icons/remove.svg");
        case Icon::Filter:
            return QStringLiteral(":/icons/filter.svg");
        case Icon::Flow:
            return QStringLiteral(":/icons/flow.svg");
        case Icon::Scene:
            return QStringLiteral(":/icons/scene.svg");
        case Icon::Preset:
            return QStringLiteral(":/icons/preset.svg");
        case Icon::Assets:
            return QStringLiteral(":/icons/assets.svg");
        case Icon::MoveUp:
            return QStringLiteral(":/icons/move-up.svg");
        case Icon::MoveDown:
            return QStringLiteral(":/icons/move-down.svg");
        case Icon::DragHandle:
            return QStringLiteral(":/icons/drag-handle.svg");
        case Icon::Drop:
            return QStringLiteral(":/icons/drop.svg");
        case Icon::Error:
            return QStringLiteral(":/icons/error.svg");
        case Icon::Warning:
            return QStringLiteral(":/icons/warning.svg");
    }
    return {};
}

/// Whether the icon is drawn in one color, which the palette then gives it.
bool isPlain(Icon which)
{
    return which != Icon::Drop && which != Icon::Error && which != Icon::Warning;
}

/// Draws the SVG at any size, as sharp as the device allows, in the palette's color for
/// the mode when it is plain.
class SvgEngine : public QIconEngine {
public:
    explicit SvgEngine(Icon which) : m_which(which) {}

    QIconEngine* clone() const override { return new SvgEngine(m_which); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QImage image((QSizeF(rect.size()) * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter imagePainter(&image);
            QSvgRenderer renderer(fileOf(m_which));
            renderer.setAspectRatioMode(Qt::KeepAspectRatio);
            renderer.render(&imagePainter);
            if (isPlain(m_which)) {
                const QPalette palette = QGuiApplication::palette();
                imagePainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                imagePainter.fillRect(image.rect(),
                                      mode == QIcon::Disabled
                                          ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                                      : mode == QIcon::Selected
                                          ? palette.color(QPalette::HighlightedText)
                                          : palette.color(QPalette::ButtonText));
            }
        }
        image.setDevicePixelRatio(ratio);
        painter->drawImage(rect, image);
    }

    // QIconEngine's own leaves the pixmap unfilled.
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }

private:
    Icon m_which;
};

}  // namespace

QIcon icon(Icon which)
{
    return QIcon(new SvgEngine(which));
}

void paintDragHandle(QPainter& painter, const QRect& rect, Qt::Orientation orientation)
{
    // The dots fill the middle half of the drawing, so twice the handle's breadth fits them.
    const int size = 2 * (orientation == Qt::Vertical ? rect.width() : rect.height());
    if (size <= 0) {
        return;
    }
    painter.save();
    painter.translate(QRectF(rect).center());
    if (orientation == Qt::Horizontal) {
        painter.rotate(90);
    }
    icon(Icon::DragHandle).paint(&painter, QRect(-size / 2, -size / 2, size, size));
    painter.restore();
}

void setMessageIcon(QMessageBox& box, Icon which)
{
    const int size = box.style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, &box);
    box.setIconPixmap(icon(which).pixmap(QSize(size, size), box.devicePixelRatioF()));
}

QMessageBox::StandardButton warning(QWidget* parent, const QString& title, const QString& text,
                                    QMessageBox::StandardButtons buttons,
                                    QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(QMessageBox::Warning, title, text, buttons, parent);
    box.setDefaultButton(defaultButton);
    setMessageIcon(box, Icon::Warning);
    return QMessageBox::StandardButton(box.exec());
}

}  // namespace shoal::editor
