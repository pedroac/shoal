#pragma once

#include <QGraphicsView>
#include <QPoint>

namespace shoal::editor {

class FlowScene;

/// Shows a FlowScene: Ctrl+wheel zooms, the middle button pans, the left button selects,
/// drags nodes and draws transitions from their ports, and the context menu edits.
class FlowView : public QGraphicsView {
    Q_OBJECT
public:
    explicit FlowView(FlowScene& flowScene, QWidget* parent = nullptr);

    FlowScene& flowScene() const { return m_flowScene; }
    double zoom() const;

public slots:
    /// Adds a scene in the middle of the view.
    void addScene();
    /// Asks for a new id for the one selected scene.
    void renameSelectedScene();
    void setSelectedSceneAsStart();
    void removeSelection();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitAll();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void setZoom(double zoom);
    void promptRename(const QString& sceneId);

    FlowScene& m_flowScene;
    bool m_panning = false;
    QPoint m_panFrom;
};

}  // namespace shoal::editor
