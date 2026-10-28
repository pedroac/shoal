#include "editor/SheetBrowserPanel.h"

#include <QDir>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "assets/SheetCache.h"

namespace shoal::editor {

namespace {

constexpr int ICON_SIZE = 40;  ///< On screen, in pixels.

enum Role { KIND = Qt::UserRole, SHEET, NAME };
enum Kind { SHEET_ROW, GROUP_ROW, SPRITE_ROW, ANIMATION_ROW };

/// `sprite` drawn whole in a square, upright and flipped as the canvas draws it, scaled to
/// fill the square; pixel art stays sharp.
QIcon thumbnail(const assets::Sheet& sheet, const assets::Sprite& sprite, const QPixmap& atlas,
                bool flipX, bool flipY, double pixelRatio)
{
    const assets::SpritePlacement placement =
        assets::placeSprite(sprite, 1.0 / sheet.scale, flipX, flipY);
    const QRectF bounds = placement.bounds;
    if (atlas.isNull() || bounds.isEmpty()) {
        return {};
    }
    const int pixels = int(ICON_SIZE * pixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const double factor = std::min(pixels / bounds.width(), pixels / bounds.height());
    painter.translate(pixels / 2.0, pixels / 2.0);
    painter.scale(factor, factor);
    painter.translate(-bounds.center());
    painter.setTransform(placement.transform, true);
    painter.drawPixmap(placement.target, atlas, placement.source);
    painter.end();
    pixmap.setDevicePixelRatio(pixelRatio);
    return QIcon(pixmap);
}

QTreeWidgetItem* groupRow(QTreeWidgetItem* sheetRow, const QString& text)
{
    auto* row = new QTreeWidgetItem(sheetRow, {text});
    row->setData(0, KIND, GROUP_ROW);
    row->setFlags(Qt::ItemIsEnabled);
    return row;
}

}  // namespace

/// Drags the selected sprite or animation, never a sheet or a group.
class SheetBrowserPanel::Tree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {QLatin1StringView(SheetItem::MIME_TYPE)}; }

    QMimeData* mimeData(const QList<QTreeWidgetItem*>& rows) const override
    {
        for (const QTreeWidgetItem* row : rows) {
            if (const std::optional<SheetItem> item = itemOf(row)) {
                return item->toMimeData();
            }
        }
        return nullptr;
    }
};

SheetBrowserPanel::SheetBrowserPanel(assets::SheetCache& sheets, QWidget* parent)
    : QWidget(parent), m_sheets(sheets), m_tree(new Tree(this)), m_filter(new QLineEdit(this))
{
    m_filter->setObjectName(QStringLiteral("sheetFilter"));
    m_filter->setPlaceholderText(tr("Filter sprites and animations"));
    m_filter->setClearButtonEnabled(true);
    m_tree->setObjectName(QStringLiteral("sheetTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree);

    connect(m_filter, &QLineEdit::textChanged, this, &SheetBrowserPanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* row) {
        if (const std::optional<SheetItem> item = itemOf(row)) {
            emit placeRequested(*item);
        }
    });
    connect(&m_sheets, &assets::SheetCache::sheetChanged, this, &SheetBrowserPanel::onSheetChanged);
    for (const QString& alias : m_sheets.aliases()) {
        onSheetChanged(alias);
    }
}

QTreeWidget* SheetBrowserPanel::tree() const
{
    return m_tree;
}

QTreeWidgetItem* SheetBrowserPanel::sheetRow(const QString& alias) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_tree->topLevelItem(i);
        if (row->data(0, SHEET).toString() == alias) {
            return row;
        }
    }
    return nullptr;
}

QTreeWidgetItem* SheetBrowserPanel::itemRow(const SheetItem& item) const
{
    QTreeWidgetItem* sheet = sheetRow(item.sheet);
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

std::optional<SheetItem> SheetBrowserPanel::itemOf(const QTreeWidgetItem* row)
{
    const int kind = row ? row->data(0, KIND).toInt() : SHEET_ROW;
    if (kind != SPRITE_ROW && kind != ANIMATION_ROW) {
        return std::nullopt;
    }
    return SheetItem{kind == ANIMATION_ROW ? SheetItem::Kind::Animation : SheetItem::Kind::Sprite,
                     row->data(0, SHEET).toString(), row->data(0, NAME).toString()};
}

void SheetBrowserPanel::setFilter(const QString& text)
{
    m_filter->setText(text);  // applies it
}

void SheetBrowserPanel::onSheetChanged(const QString& alias)
{
    QTreeWidgetItem* row = sheetRow(alias);
    if (!m_sheets.aliases().contains(alias)) {
        delete row;  // no longer declared
        return;
    }
    if (!row) {
        // Sheets in alias order, as the manifest writes them.
        int index = 0;
        while (index < m_tree->topLevelItemCount() &&
               m_tree->topLevelItem(index)->data(0, SHEET).toString() < alias) {
            ++index;
        }
        row = new QTreeWidgetItem({alias});
        row->setData(0, KIND, SHEET_ROW);
        row->setData(0, SHEET, alias);
        row->setFlags(Qt::ItemIsEnabled);
        m_tree->insertTopLevelItem(index, row);
        row->setExpanded(true);
    }
    fillSheetRow(row, alias);
    applyFilter();
}

void SheetBrowserPanel::fillSheetRow(QTreeWidgetItem* row, const QString& alias)
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
                              : QColor(200, 60, 40));
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
            spriteRow->setIcon(
                0, thumbnail(*sheet, sprite, atlasOf(sprite), false, false, pixelRatio));
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
                animationRow->setIcon(0, thumbnail(*sheet, first, atlasOf(first), animation.flipX,
                                                   animation.flipY, pixelRatio));
            }
            animationRow->setToolTip(
                0, tr("%n frame(s) at %1 fps", nullptr, int(animation.frames.size()))
                       .arg(animation.fps));
            animationRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                   Qt::ItemIsDragEnabled);
        }
    }
}

void SheetBrowserPanel::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem* sheet = m_tree->topLevelItem(s);
        bool sheetShown = text.isEmpty();
        for (int g = 0; g < sheet->childCount(); ++g) {
            QTreeWidgetItem* group = sheet->child(g);
            bool groupShown = false;
            for (int i = 0; i < group->childCount(); ++i) {
                QTreeWidgetItem* row = group->child(i);
                const bool shown = row->text(0).contains(text, Qt::CaseInsensitive);
                row->setHidden(!shown);
                groupShown = groupShown || shown;
            }
            group->setHidden(!groupShown);
            sheetShown = sheetShown || groupShown;
        }
        sheet->setHidden(!sheetShown);
    }
}

}  // namespace shoal::editor
