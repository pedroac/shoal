#include "editor/PresetsPanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/PresetCommands.h"
#include "core/Presets.h"
#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "editor/ClickOrDragView.h"

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

/// Drags the preset of the row, as its id, without making it current (see
/// ClickOrDragView).
class PresetsPanel::List : public ClickOrDragView<QListWidget> {
public:
    using ClickOrDragView::ClickOrDragView;

protected:
    QStringList mimeTypes() const override { return {QLatin1StringView(MIME_TYPE)}; }

    QMimeData* mimeData(const QList<QListWidgetItem*>& rows) const override
    {
        return rows.isEmpty() ? nullptr
                              : PresetsPanel::mimeData(rows.first()->data(PRESET_ID).toString());
    }
};

PresetsPanel::PresetsPanel(QWidget* parent)
    : QWidget(parent),
      m_list(new List(this)),
      m_newButton(new QToolButton(this)),
      m_duplicateButton(new QToolButton(this)),
      m_removeButton(new QToolButton(this))
{
    m_list->setObjectName(QStringLiteral("presetList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);

    m_newButton->setObjectName(QStringLiteral("newPresetButton"));
    m_newButton->setText(tr("New"));
    m_newButton->setToolTip(tr("A new preset, creating one mark"));
    m_duplicateButton->setObjectName(QStringLiteral("duplicatePresetButton"));
    m_duplicateButton->setText(tr("Duplicate"));
    m_duplicateButton->setToolTip(tr("Copy the preset into a new one"));
    m_removeButton->setObjectName(QStringLiteral("removePresetButton"));
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setToolTip(tr("Remove the project's preset"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_newButton);
    buttons->addWidget(m_duplicateButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    connect(m_newButton, &QToolButton::clicked, this, &PresetsPanel::newPreset);
    connect(m_duplicateButton, &QToolButton::clicked, this, &PresetsPanel::duplicatePreset);
    connect(m_removeButton, &QToolButton::clicked, this, &PresetsPanel::removePreset);
    connect(this, &PresetsPanel::currentPresetChanged, this, &PresetsPanel::updateButtons);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* row) {
        if (row->flags() & Qt::ItemIsDragEnabled) {  // readable
            emit placeRequested(row->data(PRESET_ID).toString());
        }
    });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this] { emit currentPresetChanged(currentPreset()); });
    setDocument(nullptr);
}

void PresetsPanel::setDocument(core::ProjectDocument* document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document) {
        connect(m_document, &core::ProjectDocument::presetChanged, this, &PresetsPanel::rebuild);
        // The current row follows a rename.
        connect(m_document, &core::ProjectDocument::presetRenamed, this,
                [this](const QString& from, const QString& to) {
                    if (QListWidgetItem* row = m_list->currentItem();
                        row && row->data(PRESET_ID).toString() == from) {
                        row->setData(PRESET_ID, to);
                    }
                });
    }
    setEnabled(m_document != nullptr);
    rebuild();
}

QString PresetsPanel::currentPreset() const
{
    const QListWidgetItem* row = m_list->currentItem();
    return row ? row->data(PRESET_ID).toString() : QString();
}

void PresetsPanel::setCurrentPreset(const QString& presetId)
{
    m_list->setCurrentItem(presetRow(presetId));
}

QString PresetsPanel::newPreset()
{
    if (!m_document) {
        return {};
    }
    const QString id = core::uniquePresetId(m_document->project(), QStringLiteral("preset"));
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, id, core::presetOf(tr("New Preset"), {core::Entity{}}),
        tr("Add preset %1").arg(id)));
    setCurrentPreset(id);
    emit editRequested(id);
    return id;
}

QString PresetsPanel::duplicatePreset()
{
    const QString from = currentPreset();
    const QMap<QString, core::Preset> presets =
        m_document ? core::availablePresets(m_document->project()) : QMap<QString, core::Preset>();
    if (!presets.contains(from)) {
        return {};
    }
    const QString id = core::uniquePresetId(m_document->project(), from);
    core::Preset copy = presets.value(from);
    const QString name = copy.content.value(QStringLiteral("name")).toString(from);
    copy.content.insert(QStringLiteral("name"), tr("%1 Copy").arg(name));
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, id, std::move(copy), tr("Duplicate preset %1").arg(from)));
    setCurrentPreset(id);
    emit editRequested(id);
    return id;
}

void PresetsPanel::removePreset()
{
    const QString id = currentPreset();
    if (!m_document || !m_document->project().presets.contains(id)) {
        return;  // a built-in one stays
    }
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, id, std::nullopt, tr("Remove preset %1").arg(id)));
}

void PresetsPanel::updateButtons()
{
    const QString id = currentPreset();
    m_newButton->setEnabled(m_document != nullptr);
    m_duplicateButton->setEnabled(!id.isEmpty());
    m_removeButton->setEnabled(m_document && m_document->project().presets.contains(id));
    m_removeButton->setText(core::builtInPresets().contains(id) ? tr("Revert") : tr("Remove"));
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
    const QString current = currentPreset();
    {
        const QSignalBlocker blocker(m_list);  // the current one comes back below
        m_list->clear();
    }
    if (!m_document) {
        if (!current.isEmpty()) {
            emit currentPresetChanged({});
        }
        updateButtons();
        return;
    }
    const QMap<QString, core::Preset> presets = core::availablePresets(m_document->project());
    const QColor outline = palette().color(QPalette::Text);
    const auto& own = m_document->project().presets;
    const QStringList builtInIds =
        core::builtInPresets(m_document->project().manifest.designResolution).keys();
    // Sorted by what they show, so built-in and project presets mix.
    std::vector<std::pair<QString, QListWidgetItem*>> rows;
    for (auto it = presets.cbegin(); it != presets.cend(); ++it) {
        const core::PresetRead read = core::readPresetDefinition(it.key(), *it);
        auto* row = new QListWidgetItem;
        row->setData(PRESET_ID, it.key());
        const bool builtIn = builtInIds.contains(it.key());
        const QString origin =
            !own.contains(it.key()) ? tr("built in")
            : builtIn ? tr("%1, customizing a built-in one").arg(core::paths::preset(it.key()))
                      : core::paths::preset(it.key());
        if (read.definition) {
            row->setText(read.definition->name);
            row->setIcon(swatch(QColor(read.definition->swatch), outline));
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
    {
        const QSignalBlocker blocker(m_list);
        for (const auto& [key, row] : rows) {
            m_list->addItem(row);
        }
        m_list->setCurrentItem(presetRow(current));
    }
    if (currentPreset() != current) {
        emit currentPresetChanged(currentPreset());  // it was removed
    }
    updateButtons();
}

}  // namespace shoal::editor
