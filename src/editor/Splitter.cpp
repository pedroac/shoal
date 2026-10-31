#include "editor/Splitter.h"

#include <QPainter>
#include <QSplitterHandle>

#include "editor/Icons.h"

namespace shoal::editor {

namespace {

constexpr int HANDLE_WIDTH = 8;  ///< Room for the dots, in pixels.

class Handle : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QSplitterHandle::paintEvent(event);
        QPainter painter(this);
        // Across the handle: upright between panels side by side.
        paintDragHandle(painter, rect(),
                        orientation() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
    }
};

}  // namespace

Splitter::Splitter(Qt::Orientation orientation, QWidget* parent) : QSplitter(orientation, parent)
{
    setHandleWidth(HANDLE_WIDTH);
}

QSplitterHandle* Splitter::createHandle()
{
    return new Handle(orientation(), this);
}

}  // namespace shoal::editor
