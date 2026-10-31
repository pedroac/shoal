#pragma once

#include <QPointer>
#include <QWidget>
#include <optional>

#include "editor/SheetItem.h"

class QAction;
class QCompleter;
class QLineEdit;
class QStandardItemModel;
class QToolButton;

namespace shoal::assets {
class SheetCache;
}

namespace shoal::editor {

/// The inspector's field for an entity's image: a sprite or an animation of a sheet, or a
/// standalone image, shown with its thumbnail by its path in the asset browser
/// ("Sprites / hero / Animations / run", see SheetItem::path()).
///
/// It is chosen by dropping it from the asset browser, by typing (the popup lists the assets
/// whose path contains the text, with thumbnails; Enter or a click chooses one), or from the
/// whole list (the browse button); the clear button takes it away. Text that names no asset
/// is shown in red and never chosen: Escape, or leaving the field, shows the image again. An
/// image whose asset is gone stays, shown in red as missing.
///
/// It only shows: choosing emits chosen(), and the entity, changed, sets it back.
class AssetField : public QWidget {
    Q_OBJECT
public:
    explicit AssetField(QWidget* parent = nullptr);

    /// What it lists, and follows; null lists nothing.
    void setSheets(assets::SheetCache* sheets);
    /// Shows `item`, or none; text being typed stays.
    void setItem(const std::optional<SheetItem>& item);
    const std::optional<SheetItem>& item() const { return m_item; }

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
    /// choosing nothing, if no asset has that path.
    bool choose(const QString& text);

signals:
    /// The user chose `item`, or none.
    void chosen(const std::optional<shoal::editor::SheetItem>& item);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    /// Lists the cache's sprites, animations and images.
    void rebuild();
    /// Shows the item: its path, thumbnail and whether it is missing.
    void showItem();
    void showValidity();
    /// The listed asset whose path is `text`, ignoring case.
    std::optional<SheetItem> listed(const QString& text) const;
    void emitChosen(const std::optional<SheetItem>& item);

    QPointer<assets::SheetCache> m_sheets;
    std::optional<SheetItem> m_item;
    QLineEdit* m_edit;
    QAction* m_icon;
    QToolButton* m_browseButton;
    QToolButton* m_clearButton;
    QCompleter* m_completer;
    QStandardItemModel* m_assets;
};

}  // namespace shoal::editor
