#pragma once

#include <QPointer>
#include <QWidget>
#include <optional>

#include "editor/AssetItem.h"

class QAction;
class QCompleter;
class QLineEdit;
class QStandardItemModel;
class QToolButton;

namespace shoal::assets {
class FontCache;
class SheetCache;
}  // namespace shoal::assets

namespace shoal::editor {

/// An inspector field for an asset of an entity's: what one of its parts draws, or, as the
/// Asset field, any asset, whose kind says what the entity is (see Accepts). It shows
/// the asset with its thumbnail, by its path in the asset browser ("Sprites / hero /
/// Animations / run", see AssetItem::path()).
///
/// An asset is chosen by dropping it from the asset browser, by typing (the popup lists the
/// assets whose path contains the text, with thumbnails; Enter or a click chooses one), or
/// from the whole list (the browse button); the clear button takes it away. It lists and
/// takes only the assets it accepts. Text that names none is shown in red and never chosen:
/// Escape, or leaving the field, shows the asset again. An asset that is gone stays, shown
/// in red as missing.
///
/// It only shows: choosing emits chosen(), and the entity, changed, sets it back.
class AssetField : public QWidget {
    Q_OBJECT
public:
    /// What it takes: what an image draws (sprites, animations and images), fonts, audio
    /// clips, videos, or any of those, for the Asset field.
    enum class Accepts { Images, Fonts, Sounds, Videos, Any };

    explicit AssetField(Accepts accepts, QWidget* parent = nullptr);

    Accepts accepted() const { return m_accepts; }
    bool accepts(const AssetItem& item) const;

    /// What it lists, and follows; null lists none of that kind.
    void setCaches(assets::SheetCache* sheets, assets::FontCache* fonts);
    /// The audio and video aliases the manifest declares, which Shoal lists without decoding
    /// them.
    void setClips(const QStringList& audio, const QStringList& videos);
    /// Shows `item`, or none; text being typed stays.
    void setItem(const std::optional<AssetItem>& item);
    const std::optional<AssetItem>& item() const { return m_item; }

    QLineEdit* lineEdit() const { return m_edit; }
    QToolButton* browseButton() const { return m_browseButton; }
    QToolButton* clearButton() const { return m_clearButton; }
    QCompleter* completer() const { return m_completer; }

    /// Whether the item shown is gone: its alias isn't declared as its kind, or its loaded
    /// sheet has no sprite or animation of its name.
    bool isMissing() const;
    /// Whether the text names an asset: the item's, or one it lists; empty names none.
    bool isValid() const;
    /// Chooses the asset whose path is `text`, ignoring case, or none for empty text; false,
    /// choosing nothing, if no asset it lists has that path.
    bool choose(const QString& text);

signals:
    /// The user chose `item`, or none.
    void chosen(const std::optional<shoal::editor::AssetItem>& item);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /// Lists the assets it accepts, from the caches.
    void rebuild();
    /// Shows the item: its path, thumbnail and whether it is missing.
    void showItem();
    void showValidity();
    QIcon thumbnailOf(const AssetItem& item) const;
    /// The listed asset whose path is `text`, ignoring case.
    std::optional<AssetItem> listed(const QString& text) const;
    void emitChosen(const std::optional<AssetItem>& item);

    Accepts m_accepts;
    QPointer<assets::SheetCache> m_sheets;
    QPointer<assets::FontCache> m_fonts;
    QStringList m_audio;
    QStringList m_videos;
    std::optional<AssetItem> m_item;
    QLineEdit* m_edit;
    QAction* m_icon;
    QToolButton* m_browseButton;
    QToolButton* m_clearButton;
    QCompleter* m_completer;
    QStandardItemModel* m_assets;
};

}  // namespace shoal::editor
