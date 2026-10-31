#include "editor/AssetField.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QStandardItemModel>
#include <QToolButton>

#include "assets/SheetCache.h"
#include "assets/SpriteSheet.h"
#include "editor/Thumbnails.h"

namespace shoal::editor {

namespace {

constexpr int ICON_SIZE = 32;  ///< In the popup, on screen, in pixels.

enum Role { KIND = Qt::UserRole, SHEET, NAME };

const QColor ERROR_COLOR(200, 60, 40);

SheetItem itemAt(const QModelIndex& index)
{
    return SheetItem{SheetItem::Kind(index.data(KIND).toInt()), index.data(SHEET).toString(),
                     index.data(NAME).toString()};
}

}  // namespace

AssetField::AssetField(QWidget* parent)
    : QWidget(parent),
      m_edit(new QLineEdit(this)),
      m_icon(new QAction(this)),
      m_browseButton(new QToolButton(this)),
      m_clearButton(new QToolButton(this)),
      m_completer(new QCompleter(this)),
      m_assets(new QStandardItemModel(this))
{
    m_edit->setObjectName(QStringLiteral("assetFieldEdit"));
    m_edit->setPlaceholderText(tr("None: drop or type an asset"));
    m_edit->addAction(m_icon, QLineEdit::LeadingPosition);
    m_icon->setVisible(false);
    // Drops land on the field, which takes assets only, not text.
    m_edit->setAcceptDrops(false);
    setAcceptDrops(true);
    m_browseButton->setObjectName(QStringLiteral("assetFieldBrowse"));
    m_browseButton->setText(QStringLiteral("…"));
    m_browseButton->setToolTip(tr("Choose from all the assets"));
    m_clearButton->setObjectName(QStringLiteral("assetFieldClear"));
    m_clearButton->setText(QStringLiteral("✕"));
    m_clearButton->setToolTip(tr("No image"));

    m_completer->setModel(m_assets);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setMaxVisibleItems(12);
    m_completer->popup()->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    m_edit->setCompleter(m_completer);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins({});
    layout->setSpacing(2);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_browseButton);
    layout->addWidget(m_clearButton);

    m_edit->installEventFilter(this);
    connect(m_edit, &QLineEdit::textEdited, this, &AssetField::showValidity);
    connect(m_edit, &QLineEdit::returnPressed, this, [this] { choose(m_edit->text()); });
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) { emitChosen(itemAt(index)); });
    connect(m_browseButton, &QToolButton::clicked, this, [this] {
        m_edit->setFocus(Qt::PopupFocusReason);
        m_completer->setCompletionPrefix({});
        m_completer->complete();
    });
    connect(m_clearButton, &QToolButton::clicked, this, [this] { emitChosen(std::nullopt); });
    showItem();
}

void AssetField::setSheets(assets::SheetCache* sheets)
{
    if (m_sheets == sheets) {
        return;
    }
    if (m_sheets) {
        disconnect(m_sheets, nullptr, this, nullptr);
    }
    m_sheets = sheets;
    if (m_sheets) {
        connect(m_sheets, &assets::SheetCache::sheetChanged, this, [this] {
            rebuild();
            showItem();
        });
    }
    rebuild();
    showItem();
}

void AssetField::setItem(const std::optional<SheetItem>& item)
{
    m_item = item;
    showItem();
}

bool AssetField::isMissing() const
{
    if (!m_item || !m_sheets) {
        return false;
    }
    const bool image = m_item->kind == SheetItem::Kind::Image;
    if (!m_sheets->aliases().contains(m_item->sheet) || m_sheets->isImage(m_item->sheet) != image) {
        return true;
    }
    const std::shared_ptr<const assets::Sheet> sheet = m_sheets->sheet(m_item->sheet);
    if (!sheet || image) {
        return false;  // not loaded: nothing says it's gone
    }
    return m_item->kind == SheetItem::Kind::Animation ? !sheet->animation(m_item->name)
                                                      : !sheet->sprite(m_item->name);
}

bool AssetField::isValid() const
{
    const QString text = m_edit->text().trimmed();
    return text.isEmpty() || (m_item && !m_edit->isModified()) ||
           (m_item && text.compare(m_item->path(), Qt::CaseInsensitive) == 0) ||
           listed(text).has_value();
}

bool AssetField::choose(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        emitChosen(std::nullopt);
        return true;
    }
    if (m_item && trimmed.compare(m_item->path(), Qt::CaseInsensitive) == 0) {
        showItem();  // unchanged
        return true;
    }
    const std::optional<SheetItem> item = listed(trimmed);
    if (!item) {
        showValidity();
        return false;
    }
    emitChosen(item);
    return true;
}

void AssetField::emitChosen(const std::optional<SheetItem>& item)
{
    // The entity sets it back, if it changes; either way, the field shows the entity's.
    m_edit->setModified(false);
    if (item != m_item) {
        emit chosen(item);
    }
    showItem();
}

std::optional<SheetItem> AssetField::listed(const QString& text) const
{
    for (int i = 0; i < m_assets->rowCount(); ++i) {
        const QModelIndex index = m_assets->index(i, 0);
        if (index.data().toString().compare(text, Qt::CaseInsensitive) == 0) {
            return itemAt(index);
        }
    }
    return std::nullopt;
}

void AssetField::rebuild()
{
    m_assets->clear();
    if (!m_sheets) {
        return;
    }
    const double pixelRatio = devicePixelRatioF();
    const auto add = [&](const SheetItem& item) {
        auto* row =
            new QStandardItem(itemThumbnail(*m_sheets, item, ICON_SIZE, pixelRatio), item.path());
        row->setData(int(item.kind), KIND);
        row->setData(item.sheet, SHEET);
        row->setData(item.name, NAME);
        row->setEditable(false);
        m_assets->appendRow(row);
    };
    // As the asset browser lists them: sheets, then images, each by alias.
    QStringList images;
    for (const QString& alias : m_sheets->aliases()) {
        if (m_sheets->isImage(alias)) {
            images << alias;
            continue;
        }
        const std::shared_ptr<const assets::Sheet> sheet = m_sheets->sheet(alias);
        if (!sheet) {
            continue;
        }
        for (const assets::Sprite& sprite : sheet->sprites) {
            add({SheetItem::Kind::Sprite, alias, sprite.path});
        }
        for (const assets::Animation& animation : sheet->animations) {
            add({SheetItem::Kind::Animation, alias, animation.name});
        }
    }
    for (const QString& alias : images) {
        if (m_sheets->sheet(alias)) {
            add({SheetItem::Kind::Image, alias, {}});
        }
    }
}

void AssetField::showItem()
{
    const bool missing = isMissing();
    const QIcon icon = m_item && m_sheets
                           ? itemThumbnail(*m_sheets, *m_item, ICON_SIZE, devicePixelRatioF())
                           : QIcon();
    m_icon->setIcon(icon);
    m_icon->setVisible(!icon.isNull());
    m_clearButton->setEnabled(isEnabled() && m_item.has_value());
    const QString path = m_item ? m_item->path() : QString();
    m_edit->setToolTip(missing ? tr("%1 isn't in the project's assets").arg(path) : path);
    // Keep what is being typed; anything else shows the item.
    if (!(m_edit->hasFocus() && m_edit->isModified())) {
        m_edit->setText(missing ? tr("%1 (missing)").arg(path) : path);
        m_edit->setModified(false);
        m_edit->setCursorPosition(0);
    }
    showValidity();
}

void AssetField::showValidity()
{
    const bool red = (!m_edit->isModified() && isMissing()) || !isValid();
    QPalette palette = m_edit->palette();
    palette.setColor(QPalette::Text, red ? ERROR_COLOR : QWidget::palette().color(QPalette::Text));
    m_edit->setPalette(palette);
}

void AssetField::dragEnterEvent(QDragEnterEvent* event)
{
    dragMoveEvent(event);
}

void AssetField::dragMoveEvent(QDragMoveEvent* event)
{
    if (!isEnabled() || !SheetItem::fromMimeData(event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void AssetField::dropEvent(QDropEvent* event)
{
    const std::optional<SheetItem> item = SheetItem::fromMimeData(event->mimeData());
    if (!isEnabled() || !item) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emitChosen(item);
}

bool AssetField::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && m_edit->isModified()) {
            m_edit->setModified(false);
            showItem();
            return true;
        }
        // Left with text that wasn't chosen: the item again. The popup taking focus isn't
        // leaving.
        if (event->type() == QEvent::FocusOut &&
            static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason) {
            m_edit->setModified(false);
            showItem();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AssetField::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        m_clearButton->setEnabled(isEnabled() && m_item.has_value());
    }
}

}  // namespace shoal::editor
