#include "editor/AssetBrowserPanel.h"

#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "assets/FontCache.h"
#include "assets/SheetCache.h"
#include "editor/ClickOrDragView.h"
#include "editor/Thumbnails.h"

namespace shoal::editor {

namespace {

constexpr int ICON_SIZE = 40;  ///< On screen, in pixels.

enum Role { KIND = Qt::UserRole, SHEET, NAME, FILE_PATH, ASSET_KIND };
enum Kind {
    KIND_ROW,
    SHEET_ROW,
    GROUP_ROW,
    SPRITE_ROW,
    ANIMATION_ROW,
    IMAGE_ROW,
    FONT_ROW,
    AUDIO_ROW,
    VIDEO_ROW
};

const QColor ERROR_COLOR(200, 60, 40);

QString kindTitle(core::AssetKind kind)
{
    switch (kind) {
        case core::AssetKind::Sheet:
            return AssetBrowserPanel::tr("Sprites");
        case core::AssetKind::Image:
            return AssetBrowserPanel::tr("Images");
        case core::AssetKind::Font:
            return AssetBrowserPanel::tr("Fonts");
        case core::AssetKind::Audio:
            return AssetBrowserPanel::tr("Audio");
        case core::AssetKind::Video:
            return AssetBrowserPanel::tr("Video");
    }
    return {};
}

/// Puts `row` among `parent`'s children in alias order, as the manifest writes them.
void insertByAlias(QTreeWidgetItem* parent, QTreeWidgetItem* row)
{
    const QString alias = row->data(0, SHEET).toString();
    int index = 0;
    while (index < parent->childCount() &&
           parent->child(index)->data(0, SHEET).toString() < alias) {
        ++index;
    }
    parent->insertChild(index, row);
}

/// Shows `row` if it, or one of its descendants, matches `text`; whether it does.
bool filterRow(QTreeWidgetItem* row, const QString& text)
{
    const int kind = row->data(0, KIND).toInt();
    bool shown = false;
    if (row->childCount() == 0 && kind != KIND_ROW && kind != GROUP_ROW) {
        shown = text.isEmpty() || row->text(0).contains(text, Qt::CaseInsensitive) ||
                row->data(0, FILE_PATH).toString().contains(text, Qt::CaseInsensitive);
    }
    for (int i = 0; i < row->childCount(); ++i) {
        shown = filterRow(row->child(i), text) || shown;
    }
    row->setHidden(!shown);
    return shown;
}

QTreeWidgetItem* groupRow(QTreeWidgetItem* sheetRow, const QString& text)
{
    auto* row = new QTreeWidgetItem(sheetRow, {text});
    row->setData(0, KIND, GROUP_ROW);
    row->setFlags(Qt::ItemIsEnabled);
    return row;
}

}  // namespace

/// Drags a sprite, an animation, an image, a font or a video, never a sheet or a group, without
/// making it current (see ClickOrDragView), and takes drops of files of a kind Shoal knows only.
class AssetBrowserPanel::Tree : public ClickOrDragView<QTreeWidget> {
public:
    explicit Tree(AssetBrowserPanel* panel) : ClickOrDragView(panel), m_panel(panel) {}

protected:
    void dragEnterEvent(QDragEnterEvent* event) override { dragMoveEvent(event); }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (assetFiles(event->mimeData()).isEmpty()) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        const QStringList files = assetFiles(event->mimeData());
        if (files.isEmpty()) {
            event->ignore();
            return;
        }
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit m_panel->assetFilesDropped(files);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QTreeWidget::paintEvent(event);
        if (m_panel->hasAssets()) {
            return;
        }
        QPainter painter(viewport());
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         tr("Drop sprat sheets, images, fonts, audio or video here to add "
                            "them to the project."));
    }

    QStringList mimeTypes() const override { return {QLatin1StringView(AssetItem::MIME_TYPE)}; }

    QMimeData* mimeData(const QList<QTreeWidgetItem*>& rows) const override
    {
        for (const QTreeWidgetItem* row : rows) {
            if (const std::optional<AssetItem> item = itemOf(row)) {
                return item->toMimeData();
            }
        }
        return nullptr;
    }

private:
    AssetBrowserPanel* m_panel;
};

AssetBrowserPanel::AssetBrowserPanel(assets::SheetCache& sheets, assets::FontCache& fonts,
                                     QWidget* parent)
    : QWidget(parent),
      m_sheets(sheets),
      m_fonts(fonts),
      m_tree(new Tree(this)),
      m_filter(new QLineEdit(this))
{
    m_filter->setObjectName(QStringLiteral("assetFilter"));
    m_filter->setPlaceholderText(tr("Filter assets"));
    m_filter->setClearButtonEnabled(true);
    m_tree->setObjectName(QStringLiteral("assetTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setAcceptDrops(true);  // .sprat files; Tree refuses everything else
    m_tree->viewport()->setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree);

    connect(m_filter, &QLineEdit::textChanged, this, &AssetBrowserPanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* row) {
        if (const std::optional<AssetItem> item = itemOf(row)) {
            emit placeRequested(*item);
        }
    });
    connect(&m_sheets, &assets::SheetCache::sheetChanged, this, &AssetBrowserPanel::onSheetChanged);
    for (const QString& alias : m_sheets.aliases()) {
        onSheetChanged(alias);
    }
    connect(&m_fonts, &assets::FontCache::fontChanged, this, &AssetBrowserPanel::onFontChanged);
    for (const QString& alias : m_fonts.aliases()) {
        onFontChanged(alias);
    }
}

QTreeWidget* AssetBrowserPanel::tree() const
{
    return m_tree;
}

QTreeWidgetItem* AssetBrowserPanel::kindRow(core::AssetKind kind) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_tree->topLevelItem(i);
        if (row->data(0, ASSET_KIND).toInt() == int(kind)) {
            return row;
        }
    }
    return nullptr;
}

QTreeWidgetItem* AssetBrowserPanel::makeKindRow(core::AssetKind kind)
{
    if (QTreeWidgetItem* row = kindRow(kind)) {
        return row;
    }
    // In the manifest's order of kinds.
    int index = 0;
    while (index < m_tree->topLevelItemCount() &&
           m_tree->topLevelItem(index)->data(0, ASSET_KIND).toInt() < int(kind)) {
        ++index;
    }
    auto* row = new QTreeWidgetItem({kindTitle(kind)});
    row->setData(0, KIND, KIND_ROW);
    row->setData(0, ASSET_KIND, int(kind));
    row->setFlags(Qt::ItemIsEnabled);
    QFont font = row->font(0);
    font.setBold(true);
    row->setFont(0, font);
    m_tree->insertTopLevelItem(index, row);
    row->setExpanded(true);
    return row;
}

bool AssetBrowserPanel::hasAssets() const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->childCount() > 0) {
            return true;
        }
    }
    return false;
}

QTreeWidgetItem* AssetBrowserPanel::rowOf(core::AssetKind kind, const QString& alias) const
{
    QTreeWidgetItem* group = kindRow(kind);
    for (int i = 0; group && i < group->childCount(); ++i) {
        if (group->child(i)->data(0, SHEET).toString() == alias) {
            return group->child(i);
        }
    }
    return nullptr;
}

QTreeWidgetItem* AssetBrowserPanel::sheetRow(const QString& alias) const
{
    return rowOf(core::AssetKind::Sheet, alias);
}

QTreeWidgetItem* AssetBrowserPanel::assetRow(const QString& alias) const
{
    for (int k = 0; k < m_tree->topLevelItemCount(); ++k) {
        QTreeWidgetItem* kind = m_tree->topLevelItem(k);
        for (int i = 0; i < kind->childCount(); ++i) {
            if (kind->child(i)->data(0, SHEET).toString() == alias) {
                return kind->child(i);
            }
        }
    }
    return nullptr;
}

QTreeWidgetItem* AssetBrowserPanel::itemRow(const AssetItem& item) const
{
    if (!item.isImage() || item.kind == AssetItem::Kind::Image) {
        return rowOf(item.assetKind(), item.alias);
    }
    QTreeWidgetItem* sheet = sheetRow(item.alias);
    for (int g = 0; sheet && g < sheet->childCount(); ++g) {
        QTreeWidgetItem* group = sheet->child(g);
        for (int i = 0; i < group->childCount(); ++i) {
            if (itemOf(group->child(i)) == item) {
                return group->child(i);
            }
        }
    }
    return nullptr;
}

std::optional<AssetItem> AssetBrowserPanel::itemOf(const QTreeWidgetItem* row)
{
    const int kind = row ? row->data(0, KIND).toInt() : SHEET_ROW;
    if (kind == IMAGE_ROW || kind == FONT_ROW || kind == AUDIO_ROW || kind == VIDEO_ROW) {
        return AssetItem{kind == IMAGE_ROW   ? AssetItem::Kind::Image
                         : kind == FONT_ROW  ? AssetItem::Kind::Font
                         : kind == AUDIO_ROW ? AssetItem::Kind::Audio
                                             : AssetItem::Kind::Video,
                         row->data(0, SHEET).toString(),
                         {}};
    }
    if (kind != SPRITE_ROW && kind != ANIMATION_ROW) {
        return std::nullopt;
    }
    return AssetItem{kind == ANIMATION_ROW ? AssetItem::Kind::Animation : AssetItem::Kind::Sprite,
                     row->data(0, SHEET).toString(), row->data(0, NAME).toString()};
}

void AssetBrowserPanel::setAssets(const core::Assets& assets, const QString& baseDirectory)
{
    const QDir base(baseDirectory);
    for (const core::AssetKind kind : {core::AssetKind::Audio, core::AssetKind::Video}) {
        const QMap<QString, QString>& declared = assets.of(kind);
        if (declared.isEmpty()) {
            delete kindRow(kind);
            continue;
        }
        QTreeWidgetItem* group = makeKindRow(kind);
        qDeleteAll(group->takeChildren());
        for (auto it = declared.cbegin(); it != declared.cend(); ++it) {
            const QString file = QDir::cleanPath(base.filePath(*it));
            const bool exists = QFileInfo::exists(file);
            auto* row =
                new QTreeWidgetItem(group, {exists ? it.key() : tr("%1 (missing)").arg(it.key())});
            // Shoal doesn't decode audio or video: a clip is placed by its alias, its file
            // there or not.
            row->setData(0, KIND, kind == core::AssetKind::Video ? VIDEO_ROW : AUDIO_ROW);
            row->setData(0, SHEET, it.key());
            row->setData(0, FILE_PATH, *it);
            row->setToolTip(0, QDir::toNativeSeparators(file));
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            if (!exists) {
                row->setForeground(0, ERROR_COLOR);
            }
        }
    }
    applyFilter();
    m_tree->viewport()->update();
}

void AssetBrowserPanel::setFilter(const QString& text)
{
    m_filter->setText(text);  // applies it
}

QStringList AssetBrowserPanel::assetFiles(const QMimeData* data)
{
    QStringList files;
    if (!data) {
        return files;
    }
    for (const QUrl& url : data->urls()) {
        const QFileInfo info(url.toLocalFile());
        if (url.isLocalFile() && core::assetKindForFile(info.fileName())) {
            files << info.absoluteFilePath();
        }
    }
    return files;
}

void AssetBrowserPanel::onSheetChanged(const QString& alias)
{
    const bool declared = m_sheets.aliases().contains(alias);
    const int wanted = !declared ? -1 : m_sheets.isImage(alias) ? IMAGE_ROW : SHEET_ROW;
    QTreeWidgetItem* row = assetRow(alias);
    const int kind = row ? row->data(0, KIND).toInt() : -1;
    if (row && kind != wanted && (kind == SHEET_ROW || kind == IMAGE_ROW)) {
        // No longer declared, or declared as the other kind.
        removeRow(row);
        row = nullptr;
    }
    if (!declared) {
        m_tree->viewport()->update();  // the hint, once the last asset is gone
        return;
    }
    if (!row) {
        const bool image = wanted == IMAGE_ROW;
        row = new QTreeWidgetItem({alias});
        row->setData(0, KIND, wanted);
        row->setData(0, SHEET, alias);
        row->setFlags(Qt::ItemIsEnabled);
        insertByAlias(makeKindRow(image ? core::AssetKind::Image : core::AssetKind::Sheet), row);
        row->setExpanded(true);
    }
    if (wanted == IMAGE_ROW) {
        fillImageRow(row, alias);
    }
    else {
        fillSheetRow(row, alias);
    }
    applyFilter();
}

void AssetBrowserPanel::removeRow(QTreeWidgetItem* row)
{
    QTreeWidgetItem* group = row->parent();
    delete row;
    if (group && group->childCount() == 0) {
        delete group;
    }
}

void AssetBrowserPanel::onFontChanged(const QString& alias)
{
    QTreeWidgetItem* row = rowOf(core::AssetKind::Font, alias);
    if (!m_fonts.aliases().contains(alias)) {
        if (row) {
            removeRow(row);
        }
        m_tree->viewport()->update();  // the hint, once the last asset is gone
        return;
    }
    if (!row) {
        row = new QTreeWidgetItem({alias});
        row->setData(0, KIND, FONT_ROW);
        row->setData(0, SHEET, alias);
        insertByAlias(makeKindRow(core::AssetKind::Font), row);
    }
    // Shown in itself, and dragged once it is loaded.
    const QString family = m_fonts.family(alias);
    const QString error = m_fonts.error(alias);
    const QString file = m_fonts.file(alias);
    QStringList tips{QDir::toNativeSeparators(file)};
    QFont font = this->font();
    if (!family.isEmpty()) {
        tips << family;
        font = QFont(family);
        font.setPixelSize(fontMetrics().height());
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }
    else {
        row->setFlags(Qt::ItemIsEnabled);
    }
    row->setFont(0, font);
    if (!error.isEmpty()) {
        tips << (family.isEmpty() ? tr("Couldn't load: %1") : tr("The last reload failed: %1"))
                    .arg(error);
    }
    row->setData(0, FILE_PATH, QDir::fromNativeSeparators(file));
    row->setToolTip(0, tips.join(u'\n'));
    row->setText(0, family.isEmpty() ? tr("%1 (not loaded)").arg(alias) : alias);
    row->setForeground(0, error.isEmpty() ? palette().color(QPalette::Text) : ERROR_COLOR);
    applyFilter();
}

void AssetBrowserPanel::fillImageRow(QTreeWidgetItem* row, const QString& alias)
{
    const std::shared_ptr<const assets::Sheet> sheet = m_sheets.sheet(alias);
    const QString error = m_sheets.error(alias);
    QStringList tips;
    if (sheet) {
        const assets::Sprite& sprite = sheet->sprites.front();
        tips << QDir::toNativeSeparators(sheet->file)
             << tr("%1 × %2").arg(sprite.sourceSize.width()).arg(sprite.sourceSize.height());
        row->setIcon(0, spriteThumbnail(*sheet, sprite, QPixmap::fromImage(sheet->atlases.front()),
                                        false, false, ICON_SIZE, devicePixelRatioF()));
        row->setData(0, FILE_PATH, QDir::fromNativeSeparators(sheet->file));
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }
    else {
        row->setIcon(0, {});
        row->setFlags(Qt::ItemIsEnabled);
    }
    if (!error.isEmpty()) {
        tips << (sheet ? tr("The last reload failed: %1") : tr("Couldn't load: %1")).arg(error);
    }
    row->setToolTip(0, tips.join(u'\n'));
    row->setText(0, sheet ? alias : tr("%1 (not loaded)").arg(alias));
    row->setForeground(0, error.isEmpty() ? palette().color(QPalette::Text) : ERROR_COLOR);
}

void AssetBrowserPanel::fillSheetRow(QTreeWidgetItem* row, const QString& alias)
{
    // Keep which groups were open across a reload.
    QStringList collapsed;
    for (int i = 0; i < row->childCount(); ++i) {
        if (!row->child(i)->isExpanded()) {
            collapsed << row->child(i)->data(0, NAME).toString();
        }
    }
    qDeleteAll(row->takeChildren());

    const std::shared_ptr<const assets::Sheet> sheet = m_sheets.sheet(alias);
    const QString error = m_sheets.error(alias);
    QStringList tips;
    if (sheet) {
        tips << QDir::toNativeSeparators(sheet->file);
    }
    if (!error.isEmpty()) {
        tips << (sheet ? tr("The last reload failed: %1") : tr("Couldn't load: %1")).arg(error);
    }
    if (sheet) {
        tips << sheet->warnings;
    }
    row->setToolTip(0, tips.join(u'\n'));
    row->setText(0, sheet ? alias : tr("%1 (not loaded)").arg(alias));
    row->setForeground(0, error.isEmpty() && (!sheet || sheet->warnings.isEmpty())
                              ? palette().color(QPalette::Text)
                              : ERROR_COLOR);
    if (!sheet) {
        return;
    }

    std::vector<QPixmap> atlases;
    for (const QImage& image : sheet->atlases) {
        atlases.push_back(QPixmap::fromImage(image));
    }
    const auto atlasOf = [&](const assets::Sprite& sprite) {
        return size_t(sprite.atlas) < atlases.size() ? atlases[size_t(sprite.atlas)] : QPixmap();
    };
    const double pixelRatio = devicePixelRatioF();

    const auto addGroup = [&](const QString& key, const QString& text) {
        QTreeWidgetItem* group = groupRow(row, text);
        group->setData(0, NAME, key);
        group->setExpanded(!collapsed.contains(key));
        return group;
    };
    if (!sheet->sprites.empty()) {
        QTreeWidgetItem* group = addGroup(QStringLiteral("sprites"),
                                          tr("Sprites (%1)").arg(qsizetype(sheet->sprites.size())));
        for (const assets::Sprite& sprite : sheet->sprites) {
            auto* spriteRow = new QTreeWidgetItem(group, {sprite.path});
            spriteRow->setData(0, KIND, SPRITE_ROW);
            spriteRow->setData(0, SHEET, alias);
            spriteRow->setData(0, NAME, sprite.path);
            spriteRow->setIcon(0, spriteThumbnail(*sheet, sprite, atlasOf(sprite), false, false,
                                                  ICON_SIZE, pixelRatio));
            spriteRow->setToolTip(
                0, tr("%1 × %2").arg(sprite.sourceSize.width()).arg(sprite.sourceSize.height()));
            spriteRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
    }
    if (!sheet->animations.empty()) {
        QTreeWidgetItem* group =
            addGroup(QStringLiteral("animations"),
                     tr("Animations (%1)").arg(qsizetype(sheet->animations.size())));
        for (const assets::Animation& animation : sheet->animations) {
            auto* animationRow = new QTreeWidgetItem(group, {animation.name});
            animationRow->setData(0, KIND, ANIMATION_ROW);
            animationRow->setData(0, SHEET, alias);
            animationRow->setData(0, NAME, animation.name);
            if (!animation.frames.empty() &&
                size_t(animation.frames.front()) < sheet->sprites.size()) {
                const assets::Sprite& first = sheet->sprites[size_t(animation.frames.front())];
                animationRow->setIcon(
                    0, spriteThumbnail(*sheet, first, atlasOf(first), animation.flipX,
                                       animation.flipY, ICON_SIZE, pixelRatio));
            }
            animationRow->setToolTip(
                0, tr("%n frame(s) at %1 fps", nullptr, int(animation.frames.size()))
                       .arg(animation.fps));
            animationRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                   Qt::ItemIsDragEnabled);
        }
    }
}

void AssetBrowserPanel::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        filterRow(m_tree->topLevelItem(i), text);
    }
}

}  // namespace shoal::editor
