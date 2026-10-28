#include "editor/PresetsPanel.h"

#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QVBoxLayout>

#include "core/Presets.h"
#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"

namespace shoal::editor {

namespace {

constexpr int SWATCH_SIZE = 16;

enum Role { PRESET_ID = Qt::UserRole };

QIcon swatch(const QColor& color, const QColor& outline)
{
    QPixmap pixmap(SWATCH_SIZE, SWATCH_SIZE);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(outline);
    painter.setBrush(color.isValid() ? QBrush(color) : QBrush(Qt::NoBrush));
    painter.drawRoundedRect(QRectF(0.5, 0.5, SWATCH_SIZE - 1, SWATCH_SIZE - 1), 3, 3);
    return QIcon(pixmap);
}

}  // namespace

/// Drags the preset of the row, as its id.
class PresetsPanel::List : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {QLatin1StringView(MIME_TYPE)}; }

    QMimeData* mimeData(const QList<QListWidgetItem*>& rows) const override
    {
        return rows.isEmpty() ? nullptr
                              : PresetsPanel::mimeData(rows.first()->data(PRESET_ID).toString());
    }
};

PresetsPanel::PresetsPanel(QWidget* parent) : QWidget(parent), m_list(new List(this))
{
    m_list->setObjectName(QStringLiteral("presetList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* row) {
        if (row->flags() & Qt::ItemIsDragEnabled) {  // readable
            emit placeRequested(row->data(PRESET_ID).toString());
        }
    });
    setDocument(nullptr);
}

void PresetsPanel::setDocument(core::ProjectDocument* document)
{
    m_document = document;
    setEnabled(m_document != nullptr);
    rebuild();
}

QListWidget* PresetsPanel::list() const
{
    return m_list;
}

QListWidgetItem* PresetsPanel::presetRow(const QString& presetId) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(PRESET_ID).toString() == presetId) {
            return m_list->item(i);
        }
    }
    return nullptr;
}

QMimeData* PresetsPanel::mimeData(const QString& presetId)
{
    auto* data = new QMimeData;
    data->setData(QLatin1StringView(MIME_TYPE), presetId.toUtf8());
    data->setText(presetId);
    return data;
}

std::optional<QString> PresetsPanel::presetFromMimeData(const QMimeData* data)
{
    if (!data || !data->hasFormat(QLatin1StringView(MIME_TYPE))) {
        return std::nullopt;
    }
    const QString id = QString::fromUtf8(data->data(QLatin1StringView(MIME_TYPE)));
    return core::isValidId(id) ? std::optional<QString>(id) : std::nullopt;
}

void PresetsPanel::rebuild()
{
    m_list->clear();
    if (!m_document) {
        return;
    }
    const QMap<QString, core::Preset> presets = core::availablePresets(m_document->project());
    const QColor outline = palette().color(QPalette::Text);
    const auto& own = m_document->project().presets;
    // Sorted by what they show, so built-in and project presets mix.
    std::vector<std::pair<QString, QListWidgetItem*>> rows;
    for (auto it = presets.cbegin(); it != presets.cend(); ++it) {
        const core::PresetRead read = core::readPresetDefinition(it.key(), *it);
        auto* row = new QListWidgetItem;
        row->setData(PRESET_ID, it.key());
        const QString origin =
            own.contains(it.key()) ? core::paths::preset(it.key()) : tr("built in");
        if (read.definition) {
            row->setText(read.definition->name);
            row->setIcon(swatch(QColor(read.definition->tint), outline));
            QStringList tips{QStringLiteral("<b>%1</b> (%2)").arg(it.key(), origin)};
            if (!read.definition->doc.isEmpty()) {
                tips << read.definition->doc.toHtmlEscaped();
            }
            row->setToolTip(tips.join(QStringLiteral("<br>")));
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        else {
            row->setText(tr("%1 (can't read)").arg(it.key()));
            row->setForeground(QColor(200, 60, 40));
            row->setToolTip(QStringLiteral("%1: %2").arg(origin, read.error.toHtmlEscaped()));
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
        rows.emplace_back(row->text().toLower(), row);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [key, row] : rows) {
        m_list->addItem(row);
    }
}

}  // namespace shoal::editor
