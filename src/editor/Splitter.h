#pragma once

#include <QSplitter>

namespace shoal::editor {

/// A QSplitter whose handles show the drag handle's dots, so they read as something to drag.
class Splitter : public QSplitter {
    Q_OBJECT
public:
    explicit Splitter(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    QSplitterHandle* createHandle() override;
};

}  // namespace shoal::editor
