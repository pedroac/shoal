#pragma once

#include <memory>

#include "core/SceneTree.h"
#include "editor/CanvasView.h"
#include "editor/SheetItem.h"

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
/// Sprites, animations and presets dragged from their panels drop where the cursor is.
class SceneView : public CanvasView {
    Q_OBJECT
public:
    SceneView(core::ProjectDocument& document, assets::SheetCache& sheets, const QString& sceneId,
              QWidget* parent = nullptr);
    ~SceneView() override;

    SceneCanvas& canvas() const { return *m_canvas; }

    /// Adds a sheet's sprite or animation on the current layer at `scenePosition`, as a
    /// drop does; one undo step. Empty if the current layer can't take it.
    core::EntityPath place(const SheetItem& item, QPointF scenePosition);
    /// Adds it in the middle of what the view shows.
    core::EntityPath placeAtCenter(const SheetItem& item);
    /// Adds what a preset creates, as a drop does; one undo step. Empty, with `error` set
    /// when the preset can't be read, if nothing was added.
    QList<core::EntityPath> placePreset(const QString& presetId, QPointF scenePosition,
                                        QString* error = nullptr);
    /// The middle of what the view shows, in scene coordinates.
    QPointF visibleCenter() const;

protected:
    QRectF contentRect() const override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    std::unique_ptr<SceneCanvas> m_canvas;
};

}  // namespace shoal::editor
