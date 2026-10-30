#pragma once

#include <QAbstractItemView>
#include <QApplication>
#include <QDrag>
#include <QIcon>
#include <QMimeData>
#include <QMouseEvent>
#include <QPersistentModelIndex>

namespace shoal::editor {

/// An item view whose draggable rows tell a click from a drag. Clicking one makes it the
/// current row on release; dragging one carries it away and leaves the current row as it
/// was, so dragging a sprite or a preset onto a scene changes nothing else; double-clicking
/// one makes it current and activates it. Other rows, and the keyboard, behave as usual.
template <typename View>
class ClickOrDragView : public View {
public:
    using View::View;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const QModelIndex index = draggableAt(event);
        m_pressed = index;
        if (!index.isValid()) {
            View::mousePressEvent(event);
            return;
        }
        m_pressPos = event->position().toPoint();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_pressed.isValid()) {
            View::mouseMoveEvent(event);
            return;
        }
        if ((event->position().toPoint() - m_pressPos).manhattanLength() <
            QApplication::startDragDistance()) {
            return;
        }
        const QModelIndex index = m_pressed;
        m_pressed = QPersistentModelIndex();
        QMimeData* data = this->model()->mimeData({index});
        if (!data) {
            return;
        }
        auto* drag = new QDrag(this);
        drag->setMimeData(data);
        if (const QIcon icon = index.data(Qt::DecorationRole).template value<QIcon>();
            !icon.isNull()) {
            drag->setPixmap(icon.pixmap(this->iconSize()));
        }
        drag->exec(Qt::CopyAction);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!m_pressed.isValid()) {
            View::mouseReleaseEvent(event);
            return;
        }
        const QModelIndex index = m_pressed;
        m_pressed = QPersistentModelIndex();
        this->setCurrentIndex(index);
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const QModelIndex index = draggableAt(event);
        if (!index.isValid()) {
            View::mouseDoubleClickEvent(event);
            return;
        }
        m_pressed = QPersistentModelIndex();  // the release that follows isn't a click of its own
        this->setCurrentIndex(index);
        emit this->activated(index);
        event->accept();
    }

private:
    /// The draggable row under a plain left-button event, or nothing.
    QModelIndex draggableAt(const QMouseEvent* event) const
    {
        if (event->button() != Qt::LeftButton || event->modifiers() != Qt::NoModifier) {
            return {};
        }
        const QModelIndex index = this->indexAt(event->position().toPoint());
        return index.isValid() && (index.flags() & Qt::ItemIsDragEnabled) ? index : QModelIndex();
    }

    QPersistentModelIndex m_pressed;  ///< The draggable row pressed, until moved or released.
    QPoint m_pressPos;
};

}  // namespace shoal::editor
