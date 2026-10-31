#pragma once

#include <QMap>
#include <QWidget>

#include "core/Model.h"
#include "editor/AssetItem.h"

class QLineEdit;
class QMimeData;
class QTreeWidget;
class QTreeWidgetItem;

namespace shoal::assets {
class FontCache;
class SheetCache;
}  // namespace shoal::assets

namespace shoal::editor {

/// The project's assets, a group for each kind: Sprites (each sprat sheet, with its sprites
/// and animations as thumbnails), Images, Fonts (each by its alias, in itself), Audio and
/// Video. A group shows only once it has an asset.
///
/// It only browses: clicking a row selects it. Drag a sprite, an animation, an image, a
/// font, a clip or a video onto a scene to place it there, or onto an inspector field to give it to
/// the selected entity (see AssetField); double-clicking one places it in the middle of the view.
/// It follows the caches, so a sheet written again by sprat, or an image or a font saved again,
/// shows up at once. Audio and video are listed by alias, since Shoal doesn't decode them.
///
/// Dropping files on it asks to declare those of a kind Shoal knows in the project.
class AssetBrowserPanel : public QWidget {
    Q_OBJECT
public:
    AssetBrowserPanel(assets::SheetCache& sheets, assets::FontCache& fonts,
                      QWidget* parent = nullptr);

    QTreeWidget* tree() const;
    /// The group row of a kind, or null while it has no asset.
    QTreeWidgetItem* kindRow(core::AssetKind kind) const;
    /// The row of an asset of `kind`, or null.
    QTreeWidgetItem* rowOf(core::AssetKind kind, const QString& alias) const;
    /// The row of a sheet, or null.
    QTreeWidgetItem* sheetRow(const QString& alias) const;
    /// The row of an asset of any kind, a sheet's included, or null.
    QTreeWidgetItem* assetRow(const QString& alias) const;
    /// The row of a sprite, an animation, an image, a font, a clip or a video, or null.
    QTreeWidgetItem* itemRow(const AssetItem& item) const;
    /// What a row stands for, or nothing for a group or a sheet.
    static std::optional<AssetItem> itemOf(const QTreeWidgetItem* row);

    /// Lists the audio and video of `assets`, relative to `baseDirectory`; sheets, images and
    /// fonts come from their caches.
    void setAssets(const core::Assets& assets, const QString& baseDirectory);

    /// Shows only the assets whose path or name contains `text`.
    void setFilter(const QString& text);

    /// The local files of a kind Shoal knows that `data` carries, such as files dragged
    /// from a file manager.
    static QStringList assetFiles(const QMimeData* data);

signals:
    /// A row was double-clicked or activated with the keyboard.
    void placeRequested(const shoal::editor::AssetItem& item);
    /// Files of a kind Shoal knows were dropped on the panel, as absolute paths.
    void assetFilesDropped(const QStringList& files);

private:
    class Tree;

    /// The group row of a kind, made if it has none yet.
    QTreeWidgetItem* makeKindRow(core::AssetKind kind);
    /// Whether any asset is listed.
    bool hasAssets() const;
    void onSheetChanged(const QString& alias);
    void onFontChanged(const QString& alias);
    /// Takes away `row`, and its kind's row once it has no other.
    void removeRow(QTreeWidgetItem* row);
    void fillSheetRow(QTreeWidgetItem* row, const QString& alias);
    void fillImageRow(QTreeWidgetItem* row, const QString& alias);
    void applyFilter();

    assets::SheetCache& m_sheets;
    assets::FontCache& m_fonts;
    Tree* m_tree;
    QLineEdit* m_filter;
};

}  // namespace shoal::editor
