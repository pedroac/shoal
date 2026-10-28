#pragma once

#include <QColor>
#include <QGraphicsScene>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <vector>

#include "core/Model.h"
#include "core/SceneTree.h"

namespace shoal::assets {
class SheetCache;
struct Sprite;
}  // namespace shoal::assets

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class EntityItem;
class TransformHandle;

/// One scene's world-space layers and entities, drawn with their sprites. It follows the
/// document, so undo and redo show up here, and turns what the user does into commands.
///
/// Which layers are hidden or locked, the current layer and the grid are the editor's own
/// state; none of it changes the project. Screen-space layers are laid out in a later
/// roadmap step and aren't drawn yet.
class SceneCanvas : public QGraphicsScene {
    Q_OBJECT
public:
    SceneCanvas(core::ProjectDocument& document, assets::SheetCache& sheets, QString sceneId,
                QObject* parent = nullptr);
    ~SceneCanvas() override;

    core::ProjectDocument& document() const { return m_document; }
    assets::SheetCache& sheets() const { return m_sheets; }
    /// Follows renames. Empty once the scene is removed.
    QString sceneId() const { return m_sceneId; }
    /// The scene, or null once it is removed.
    const core::Scene* scene() const;
    /// The scene's own rectangle, from the origin to its size.
    QRectF sceneBounds() const;

    EntityItem* item(const core::EntityPath& path) const;
    /// The selected entities, in tree order.
    QList<core::EntityPath> selectedPaths() const;
    void select(const QList<core::EntityPath>& paths);

    /// Where new entities go: a world-space layer.
    QString currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(const QString& layerId);
    bool isLayerHidden(const QString& layerId) const { return m_hidden.contains(layerId); }
    void setLayerHidden(const QString& layerId, bool hidden);
    bool isLayerLocked(const QString& layerId) const { return m_locked.contains(layerId); }
    void setLayerLocked(const QString& layerId, bool locked);

    bool snapping() const { return m_snapping; }
    void setSnapping(bool snapping);
    double gridSize() const { return m_gridSize; }
    void setGridSize(double size);
    /// `point` on the grid, when snapping is on.
    QPointF snap(QPointF point) const;

    // --- edits, each one undo step ---

    /// Adds `entity` at the top level of the current layer, at `scenePosition`; selects it.
    core::EntityPath addEntity(core::Entity entity, QPointF scenePosition);
    void removeSelection();
    /// Moves the selected entities by `delta`, in their parents' units.
    void nudgeSelection(QPointF delta);

    // --- for the items ---

    /// Pushes one command for the entities the user dragged; nothing if none moved.
    void commitMoves();
    /// Pushes one command setting an entity's rotation and scale, as a handle leaves them;
    /// nothing if they didn't change.
    void commitTransform(const core::EntityPath& path, double rotation, core::Point scale,
                         const QString& verb);
    /// Puts the rotate and scale handles on the one selected entity, or hides them.
    void updateHandles();
    TransformHandle* rotateHandle() const { return m_rotateHandle; }
    TransformHandle* scaleHandle() const { return m_scaleHandle; }
    bool isDragging() const { return m_dragging; }
    void setDragging(bool dragging) { m_dragging = dragging; }

    struct SpriteVisual {
        const assets::Sprite* sprite = nullptr;  ///< Null when it can't be drawn.
        QPixmap atlas;
        double drawScale = 1;
        bool flipX = false;  ///< An animation's own flips.
        bool flipY = false;
    };
    SpriteVisual spriteVisual(const QString& sheet, const QString& path) const;
    /// The first frame of an animation.
    SpriteVisual animationVisual(const QString& sheet, const QString& name) const;
    /// The color of the first declared class that has one, or `fallback`.
    QColor classColor(const QStringList& classes, const QColor& fallback) const;
    QSizeF designResolution() const;

signals:
    void currentLayerChanged(const QString& layerId);
    /// Layers were hidden, shown, locked or unlocked.
    void layerStateChanged();
    /// The scene's layers or entities changed, and the items were rebuilt.
    void contentChanged();

private:
    void rebuild();
    void clearItems();
    void createHandles();
    void addItems(const std::vector<core::Entity>& entities, core::EntityPath& path,
                  QGraphicsItem* parent, bool locked);
    void onSheetChanged(const QString& alias);

    core::ProjectDocument& m_document;
    assets::SheetCache& m_sheets;
    QString m_sceneId;
    QHash<core::EntityPath, EntityItem*> m_items;
    std::vector<QGraphicsItem*> m_layerItems;
    QStringList m_treeShape;  ///< Of the scene the items were built from.
    QString m_currentLayer;
    QSet<QString> m_hidden;
    QSet<QString> m_locked;
    bool m_snapping = false;
    double m_gridSize = 16;
    bool m_dragging = false;
    TransformHandle* m_rotateHandle = nullptr;
    TransformHandle* m_scaleHandle = nullptr;
    mutable QHash<QString, std::vector<QPixmap>> m_atlases;  ///< By sheet alias.
};

}  // namespace shoal::editor
