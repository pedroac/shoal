#pragma once

#include <QMap>
#include <QWidget>

#include "core/Model.h"
#include "editor/SheetItem.h"

class QLineEdit;
class QMimeData;
class QTreeWidget;
class QTreeWidgetItem;

namespace shoal::assets {
class SheetCache;
}

namespace shoal::editor {

/// The project's assets, a group for each kind: Sprites (each sprat sheet, with its sprites
/// and animations as thumbnails), Images, Fonts, Audio and Video. A group shows only once it
/// has an asset. Drag a sprite, an animation or an image onto a scene to place it there.
/// With one entity selected, it picks that entity's image: its sprite, animation or image is
/// the current row, or None is checked, and clicking another row, or None, sets it; dragging
/// one doesn't. With none, double-clicking a row places it in the middle of the view. It
/// follows the cache, so a sheet written again by sprat, or an image saved again, shows up at
/// once. Fonts, audio and video are listed, by alias, for now.
///
/// Dropping files on it asks to declare those of a kind Shoal knows in the project.
class AssetBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit AssetBrowserPanel(assets::SheetCache& sheets, QWidget* parent = nullptr);

    QTreeWidget* tree() const;
    /// The group row of a kind, or null while it has no asset.
    QTreeWidgetItem* kindRow(core::AssetKind kind) const;
    /// The row of a sheet, or null.
    QTreeWidgetItem* sheetRow(const QString& alias) const;
    /// The row of an asset of any kind, a sheet's included, or null.
    QTreeWidgetItem* assetRow(const QString& alias) const;
    /// The row of a sprite, an animation or an image, or null.
    QTreeWidgetItem* itemRow(const SheetItem& item) const;
    /// What a row stands for, or nothing for a group, a sheet, a font, a clip or a video.
    static std::optional<SheetItem> itemOf(const QTreeWidgetItem* row);

    /// Lists the fonts, audio and video of `assets`, relative to `baseDirectory`; sheets and
    /// images come from the cache.
    void setAssets(const core::Assets& assets, const QString& baseDirectory);

    /// Shows only the assets whose path or name contains `text`.
    void setFilter(const QString& text);

    /// The local files of a kind Shoal knows that `data` carries, such as files dragged
    /// from a file manager.
    static QStringList assetFiles(const QMimeData* data);

signals:
    /// A row was double-clicked or activated with the keyboard.
    void placeRequested(const shoal::editor::SheetItem& item);
    /// Files of a kind Shoal knows were dropped on the panel, as absolute paths.
    void assetFilesDropped(const QStringList& files);

private:
    class Tree;

    /// The group row of a kind, made if it has none yet.
    QTreeWidgetItem* makeKindRow(core::AssetKind kind);
    /// Whether any asset is listed.
    bool hasAssets() const;
    void onSheetChanged(const QString& alias);
    void fillSheetRow(QTreeWidgetItem* row, const QString& alias);
    void fillImageRow(QTreeWidgetItem* row, const QString& alias);
    void applyFilter();

    assets::SheetCache& m_sheets;
    Tree* m_tree;
    QLineEdit* m_filter;
};

}  // namespace shoal::editor
