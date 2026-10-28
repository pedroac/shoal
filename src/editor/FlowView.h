#pragma once

#include "editor/CanvasView.h"

namespace shoal::editor {

class FlowScene;

/// Shows a FlowScene: the left button selects, drags nodes and draws transitions from their
/// ports, and the context menu edits.
class FlowView : public CanvasView {
    Q_OBJECT
public:
    explicit FlowView(FlowScene& flowScene, QWidget* parent = nullptr);

    FlowScene& flowScene() const { return m_flowScene; }

public slots:
    /// Adds a scene in the middle of the view.
    void addScene();
    /// Asks for a new id for the one selected scene.
    void renameSelectedScene();
    void setSelectedSceneAsStart();
    void removeSelection();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void promptRename(const QString& sceneId);

    FlowScene& m_flowScene;
};

}  // namespace shoal::editor
