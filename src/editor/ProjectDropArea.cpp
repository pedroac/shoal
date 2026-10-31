#include "editor/ProjectDropArea.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QUrl>

#include "core/ProjectCodec.h"
#include "editor/Icons.h"

namespace shoal::editor {

namespace {
constexpr int ICON_SIZE = 64;
constexpr int SPACING = 12;
constexpr int MARGIN = 24;
}  // namespace

ProjectDropArea::ProjectDropArea(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
}

QString ProjectDropArea::projectIn(const QList<QUrl>& urls)
{
    if (urls.size() != 1 || !urls.front().isLocalFile()) {
        return {};
    }
    const QFileInfo info(urls.front().toLocalFile());
    if (info.isDir() || info.fileName() == core::paths::MANIFEST) {
        return info.absoluteFilePath();
    }
    return {};
}

void ProjectDropArea::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRect area = rect().adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN);
    if (m_hovered) {
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(40);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRoundedRect(area, 8, 8);
    }
    const QString text = tr("No project open\nDrop a project's folder here to open it");
    const QRect textBounds =
        painter.fontMetrics().boundingRect(area, Qt::AlignHCenter | Qt::TextWordWrap, text);
    const int top = area.center().y() - (ICON_SIZE + SPACING + textBounds.height()) / 2;
    icon(Icon::Drop)
        .paint(&painter, QRect(area.center().x() - ICON_SIZE / 2, top, ICON_SIZE, ICON_SIZE));
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(
        QRect(area.left(), top + ICON_SIZE + SPACING, area.width(), textBounds.height()),
        Qt::AlignHCenter | Qt::TextWordWrap, text);
}

void ProjectDropArea::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls() || projectIn(event->mimeData()->urls()).isEmpty()) {
        return;  // ignored
    }
    event->acceptProposedAction();
    m_hovered = true;
    update();
}

void ProjectDropArea::dragLeaveEvent(QDragLeaveEvent*)
{
    m_hovered = false;
    update();
}

void ProjectDropArea::dropEvent(QDropEvent* event)
{
    m_hovered = false;
    update();
    const QString path = projectIn(event->mimeData()->urls());
    if (path.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    emit projectDropped(path);
}

}  // namespace shoal::editor
