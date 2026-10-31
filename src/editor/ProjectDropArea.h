#pragma once

#include <QWidget>

namespace shoal::editor {

/// What the window shows with no project open: a place to drop a project's folder, or its
/// manifest, to open it.
class ProjectDropArea : public QWidget {
    Q_OBJECT
public:
    explicit ProjectDropArea(QWidget* parent = nullptr);

    /// The folder or the file a drop of `urls` would open, or empty when it opens nothing.
    static QString projectIn(const QList<QUrl>& urls);

signals:
    void projectDropped(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool m_hovered = false;  ///< Something that opens is being dragged over it.
};

}  // namespace shoal::editor
