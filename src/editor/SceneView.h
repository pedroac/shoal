#pragma once

#include <memory>

#include "core/SceneTree.h"
#include "editor/AssetItem.h"
#include "editor/CanvasView.h"

namespace shoal::assets {
class FontCache;
class SheetCache;
}  // namespace shoal::assets
namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class SceneCanvas;

/// A scene's tab: its canvas, with the scene's rectangle behind the entities.
/// Arrow keys nudge the selection by a unit, or a grid cell when snapping (ten with Shift).
/// Assets and presets dragged from their panels drop where the cursor is.
class SceneView : public CanvasView {
    Q_OBJECT
public:
    SceneView(core::ProjectDocument& document, assets::SheetCache& sheets, assets::FontCache& fonts,
              const QString& sceneId, QWidget* parent = nullptr);
    ~SceneView() override;

    SceneCanvas& canvas() const { return *m_canvas; }

    /// Adds an asset in the current parent at `scenePosition`, as a drop does (see
    /// entityFor()); one undo step. Empty if the current parent can't take it.
    core::EntityPath place(const AssetItem& item, QPointF scenePosition);
    /// The entity placing `item` adds, with no id: one drawing an image; a text, "Text",
    /// in a rect sized to it in the font; a sound source with no radius; or a video in a rect of
    /// SceneCanvas::newVideoSize(); the text and the video centered on their origin.
    core::Entity entityFor(const AssetItem& item) const;
    /// Adds it in the middle of what the view shows.
    core::EntityPath placeAtCenter(const AssetItem& item);
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
    /// On an entity: arrange or remove the selection, selecting the entity first if it
    /// isn't selected.
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    std::unique_ptr<SceneCanvas> m_canvas;
};

}  // namespace shoal::editor
