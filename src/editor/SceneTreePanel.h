#pragma once

#include <QPointer>
#include <QWidget>

#include "core/SceneTree.h"

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace shoal::editor {

class SceneCanvas;

/// The layers of the open scene, back to front, each with its entities as a tree: select
/// entities, pick the layer new entities go on, hide or lock layers, and drag entities to
/// reorder them, give them a parent, or move them to another layer.
class SceneTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit SceneTreePanel(QWidget* parent = nullptr);

    /// Shows `canvas`'s scene; null shows nothing.
    void setCanvas(SceneCanvas* canvas);
    SceneCanvas* canvas() const { return m_canvas; }
    QTreeWidget* tree() const;

    /// The row of a layer or an entity, or null.
    QTreeWidgetItem* layerRow(const QString& layerId) const;
    QTreeWidgetItem* entityRow(const core::EntityPath& path) const;

    /// Moves `from` so that it is dropped on, above or below `target` (a layer or an
    /// entity row), as a drag in the tree does; one undo step. False if it can't go there.
    enum class Drop { On, Above, Below };
    bool dropEntity(const core::EntityPath& from, QTreeWidgetItem* target, Drop drop);

public slots:
    void addLayer();
    void removeCurrentLayer();
    void moveCurrentLayerBack();
    void moveCurrentLayerFront();

private:
    class Tree;

    void rebuild();
    void updateButtons();
    void onTreeSelectionChanged();
    void onCanvasSelectionChanged();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& position);
    void renameLayer(const QString& layerId);
    int currentLayerIndex() const;

    QPointer<SceneCanvas> m_canvas;
    Tree* m_tree;
    QToolButton* m_addButton;
    QToolButton* m_removeButton;
    QToolButton* m_backButton;
    QToolButton* m_frontButton;
    bool m_syncing = false;
};

}  // namespace shoal::editor
