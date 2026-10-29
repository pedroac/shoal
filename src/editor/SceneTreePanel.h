#pragma once

#include <QPointer>
#include <QWidget>

#include "core/SceneTree.h"

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class SceneCanvas;

/// The open scene's entities, back to front, as a tree under a row for the top level, the
/// scene itself: select entities, pick the parent new entities go into, hide or lock
/// entities in the editor, and drag entities to reorder them or give them another parent.
/// The top level's row is selected when no entity is; selecting it clears the selection,
/// and the inspector shows the scene's settings.
class SceneTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit SceneTreePanel(QWidget* parent = nullptr);

    /// Shows `canvas`'s scene; null shows nothing.
    void setCanvas(SceneCanvas* canvas);
    SceneCanvas* canvas() const { return m_canvas; }
    QTreeWidget* tree() const;

    /// The row of an entity, or null; an empty path is the top level's row.
    QTreeWidgetItem* entityRow(const core::EntityPath& path) const;
    QTreeWidgetItem* topLevelRow() const { return entityRow({}); }

    /// Moves `from` so that it is dropped on, above or below `target`, as a drag in the tree
    /// does; a null `target` is the empty space after the rows, and dropping on the top
    /// level's row, its end. One undo step; false if it can't go there.
    enum class Drop { On, Above, Below };
    bool dropEntity(const core::EntityPath& from, QTreeWidgetItem* target, Drop drop);

public slots:
    /// Adds an empty entity, a group to put others in, into the current parent.
    void addGroup();
    void removeSelection();

private:
    class Tree;

    void rebuild();
    void updateButtons();
    void onTreeSelectionChanged();
    void onCanvasSelectionChanged();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& position);

    QPointer<SceneCanvas> m_canvas;
    QPointer<core::ProjectDocument> m_document;
    Tree* m_tree;
    QToolButton* m_addButton;
    QToolButton* m_removeButton;
    bool m_syncing = false;
};

}  // namespace shoal::editor
