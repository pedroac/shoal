#pragma once

#include <QWidget>

#include "editor/SheetItem.h"

class QLineEdit;
class QToolButton;
class QMimeData;
class QTreeWidget;
class QTreeWidgetItem;

namespace shoal::assets {
class SheetCache;
}

namespace shoal::editor {

/// The project's sprat sheets, each with its sprites and animations as thumbnails. Drag
/// one onto a scene to place it there. With one entity selected, it picks that entity's
/// image: its sprite or animation is the current row, or None is checked, and clicking
/// another row, or None, sets it; dragging a row doesn't. With none, double-clicking a row
/// places it in the middle of the view. It follows the cache, so a sheet written again by sprat
/// shows up at once.
///
/// Dropping .sprat files on it asks to declare them in the project.
class SheetBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit SheetBrowserPanel(assets::SheetCache& sheets, QWidget* parent = nullptr);

    QTreeWidget* tree() const;
    /// The row of a sheet, or null.
    QTreeWidgetItem* sheetRow(const QString& alias) const;
    /// The row of a sprite or an animation, or null.
    QTreeWidgetItem* itemRow(const SheetItem& item) const;
    /// What a row stands for, or nothing for a sheet or a group row.
    static std::optional<SheetItem> itemOf(const QTreeWidgetItem* row);

    /// Shows only the sprites and animations whose path or name contains `text`.
    void setFilter(const QString& text);

    /// Picks an entity's image, showing `current` (none for no image), or, with `picking`
    /// false, only browses.
    void setChoice(bool picking, const std::optional<SheetItem>& current);
    bool isPicking() const { return m_picking; }
    /// Checked for an entity with no image; clicking it removes the image.
    QToolButton* noneButton() const { return m_noneButton; }

    /// The local .sprat files `data` carries, such as files dragged from a file manager.
    static QStringList sheetFiles(const QMimeData* data);

signals:
    /// A row was double-clicked or activated with the keyboard, while not picking.
    void placeRequested(const shoal::editor::SheetItem& item);
    /// While picking, the user chose `item`, or None.
    void chosen(const std::optional<shoal::editor::SheetItem>& item);
    /// .sprat files were dropped on the panel, as absolute paths.
    void sheetFilesDropped(const QStringList& files);

private:
    class Tree;

    void onSheetChanged(const QString& alias);
    void fillSheetRow(QTreeWidgetItem* row, const QString& alias);
    void applyFilter();

    assets::SheetCache& m_sheets;
    Tree* m_tree;
    QLineEdit* m_filter;
    QToolButton* m_noneButton;
    bool m_picking = false;
    bool m_syncing = false;  ///< While setChoice() moves the current row.
};

}  // namespace shoal::editor
