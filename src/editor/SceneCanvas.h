#pragma once

#include <QColor>
#include <QGraphicsScene>
#include <QHash>
#include <QKeySequence>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <vector>

#include "core/Model.h"
#include "core/SceneTree.h"

class QMenu;

namespace shoal::assets {
class SheetCache;
struct Sprite;
}  // namespace shoal::assets

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class EntityItem;
class TileGridOverlay;
class TransformHandle;

/// One scene's entities, drawn with their parts. It follows the document, so undo and redo
/// show up here, and turns what the user does into commands.
///
/// Which entities are hidden or locked, the current parent and the grid are the editor's
/// own state; none of it changes the project. Screen-space entities are laid out in a later
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

    /// Where new entities go: an entity, or the top level (empty).
    core::EntityPath currentParent() const { return m_currentParent; }
    void setCurrentParent(const core::EntityPath& path);
    /// Whether the editor hides or locks the entity, with its descendants; an entity is
    /// known by its id, or by its path if it has none.
    bool isHidden(const core::EntityPath& path) const;
    void setHidden(const core::EntityPath& path, bool hidden);
    bool isLocked(const core::EntityPath& path) const;
    void setLocked(const core::EntityPath& path, bool locked);
    /// Whether it, or one of its ancestors, is hidden or locked.
    bool isHiddenOrLocked(const core::EntityPath& path) const;
    /// Whether the entity is in screen space, which isn't drawn yet.
    bool isInScreenSpace(const core::EntityPath& path) const;

    bool snapping() const { return m_snapping; }
    void setSnapping(bool snapping);
    double gridSize() const { return m_gridSize; }
    void setGridSize(double size);
    /// `point` on the grid, when snapping is on.
    QPointF snap(QPointF point) const;

    // --- edits, each one undo step ---

    /// What the current parent offers new entities to measure against: in a tiled
    /// container, they go on its tiles.
    core::Frame currentFrame() const;
    /// Whether new entities can go into the current parent: it must be in world space,
    /// shown and unlocked, or they couldn't be seen or selected.
    bool canAddEntities() const;
    /// Adds `entity` as the current parent's last child, at `scenePosition`; selects it.
    /// Empty if canAddEntities() is false.
    core::EntityPath addEntity(core::Entity entity, QPointF scenePosition);
    /// Adds what a preset creates, into the current parent around `scenePosition`, with
    /// fresh ids; selects it. Empty, with `error` set when the preset can't be read, if
    /// nothing was added.
    QList<core::EntityPath> addPreset(const QString& presetId, QPointF scenePosition,
                                      QString* error = nullptr);
    void removeSelection();
    /// Moves the selected entities in draw order among their siblings, keeping them
    /// selected. Nothing if none would move.
    void arrangeSelection(core::Arrange arrange);
    /// Moves the selected entities a step in `direction` (each axis -1, 0 or 1), in their
    /// parents' units: a unit, or a grid cell when snapping, or ten with `large`. In a
    /// tiled container they move from cell to cell instead, staying in the grid.
    void nudgeSelection(QPoint direction, bool large = false);

    // --- the arrange commands, for the menus ---

    /// How many steps a large nudge moves.
    static constexpr int NUDGE_MULTIPLIER = 10;

    static constexpr core::Arrange ARRANGES[] = {core::Arrange::ToFront, core::Arrange::Forward,
                                                 core::Arrange::Backward, core::Arrange::ToBack};
    static QString arrangeText(core::Arrange arrange);
    static QKeySequence arrangeShortcut(core::Arrange arrange);
    /// Adds the four arrange commands for the selection to a context menu, each enabled
    /// when it would move something.
    void addArrangeActions(QMenu& menu);

    // --- for the items ---

    /// Pushes one command for the entities the user dragged; nothing if none moved.
    void commitMoves();
    /// Pushes one command setting an entity's rotation and scale, as a handle leaves them;
    /// nothing if they didn't change.
    void commitTransform(const core::EntityPath& path, double rotation, core::Point scale,
                         const QString& verb);
    /// Pushes one command setting an entity's size, as the scale handle leaves it; nothing
    /// if it didn't change.
    void commitSize(const core::EntityPath& path, QSizeF size);
    /// Moves the entity at `from` to `to` (see core::MoveEntityCommand), keeping where it is
    /// drawn; one undo step.
    void moveEntity(const core::EntityPath& from, const core::EntityPath& to);
    /// Puts the rotate and scale handles on the one selected entity, or hides them.
    void updateHandles();
    /// Shows the grids of the tiled containers that help now: the selected entities', their
    /// parents', and the current parent's.
    void updateGrids();
    /// The containers whose grids are shown, in tree order.
    QList<core::EntityPath> gridPaths() const { return m_gridPaths; }
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
        QString sheet;  ///< Where the atlas comes from.
        int atlasIndex = -1;
    };
    SpriteVisual spriteVisual(const QString& sheet, const QString& path) const;
    /// The first frame of an animation.
    SpriteVisual animationVisual(const QString& sheet, const QString& name) const;
    /// What an image draws.
    SpriteVisual visualOf(const core::Image& image) const;
    /// The natural size and pivot of images, from the sheets: what core needs to place an
    /// entity whose size or pivot is automatic.
    core::ImageMetricsFn imageMetrics() const;
    /// The atlas `visual` draws from, multiplied by `tint`'s color (its alpha is the
    /// painter's to apply).
    QPixmap tintedAtlas(const SpriteVisual& visual, const core::Color& tint) const;
    /// The color of the first declared class that has one, or `fallback`.
    QColor classColor(const QStringList& classes, const QColor& fallback) const;
    QSizeF designResolution() const;

signals:
    void currentParentChanged();
    /// Entities were hidden, shown, locked or unlocked in the editor.
    void entityStateChanged();
    /// The scene's entities changed, and the items were rebuilt or updated.
    void contentChanged();

private:
    void rebuild();
    void clearItems();
    void createHandles();
    /// Creates the items of `entities`, children of `parent` (null at the top level), or
    /// updates them in place when `create` is false.
    void placeItems(const std::vector<core::Entity>& entities, core::EntityPath& path,
                    const core::Frame& frame, EntityItem* parent, const core::Color& tint,
                    bool screen, bool blocked, bool create);
    QString stateKey(const core::EntityPath& path) const;
    void onSheetChanged(const QString& alias);

    core::ProjectDocument& m_document;
    assets::SheetCache& m_sheets;
    QString m_sceneId;
    QHash<core::EntityPath, EntityItem*> m_items;
    QStringList m_treeShape;  ///< Of the scene the items were built from.
    core::EntityPath m_currentParent;
    QString m_currentParentId;  ///< Its id, if it has one, so it is found where it moves.
    QSet<QString> m_hidden;     ///< By stateKey().
    QSet<QString> m_locked;
    bool m_snapping = false;
    double m_gridSize = 16;
    bool m_dragging = false;
    TransformHandle* m_rotateHandle = nullptr;
    TransformHandle* m_scaleHandle = nullptr;
    TileGridOverlay* m_gridOverlay = nullptr;
    QList<core::EntityPath> m_gridPaths;
    mutable QHash<QString, std::vector<QPixmap>> m_atlases;  ///< By sheet alias.
    mutable QHash<QString, QPixmap> m_tintedAtlases;         ///< By sheet, atlas and color.
};

}  // namespace shoal::editor
