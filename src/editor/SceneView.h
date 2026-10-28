#pragma once

#include <memory>

#include "editor/CanvasView.h"

namespace shoal::assets {
class SheetCache;
}
namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class SceneCanvas;

/// A scene's tab: its canvas, with the scene's rectangle and the grid behind the entities.
/// Arrow keys nudge the selection by a unit, or a grid cell when snapping (ten with Shift).
class SceneView : public CanvasView {
    Q_OBJECT
public:
    SceneView(core::ProjectDocument& document, assets::SheetCache& sheets, const QString& sceneId,
              QWidget* parent = nullptr);
    ~SceneView() override;

    SceneCanvas& canvas() const { return *m_canvas; }

protected:
    QRectF contentRect() const override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::unique_ptr<SceneCanvas> m_canvas;
};

}  // namespace shoal::editor
