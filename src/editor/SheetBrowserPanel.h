#pragma once

#include <QWidget>

#include "editor/SheetItem.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace shoal::assets {
class SheetCache;
}

namespace shoal::editor {

/// The project's sprat sheets, each with its sprites and animations as thumbnails. Drag
/// one onto a scene to place it there, or double-click it to place it in the middle of the
/// view. It follows the cache, so a sheet written again by sprat shows up at once.
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

signals:
    /// A row was double-clicked or activated with the keyboard.
    void placeRequested(const shoal::editor::SheetItem& item);

private:
    class Tree;

    void onSheetChanged(const QString& alias);
    void fillSheetRow(QTreeWidgetItem* row, const QString& alias);
    void applyFilter();

    assets::SheetCache& m_sheets;
    Tree* m_tree;
    QLineEdit* m_filter;
};

}  // namespace shoal::editor
