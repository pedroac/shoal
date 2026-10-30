#include "editor/PresetSettingsPanel.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/PresetCommands.h"
#include "core/Presets.h"
#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "editor/ColorButton.h"
#include "editor/FormFields.h"
#include "editor/InspectorPanel.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

namespace {

const QString NAME_KEY = QStringLiteral("name");
const QString DOC_KEY = QStringLiteral("doc");
const QString SWATCH_KEY = QStringLiteral("swatch");
const QString CLASSES_KEY = QStringLiteral("classes");

}  // namespace

PresetSettingsPanel::PresetSettingsPanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_idField(new QLineEdit(this)),
      m_nameField(new QLineEdit(this)),
      m_classesField(new QLineEdit(this)),
      m_swatchBox(new QCheckBox(this)),
      m_swatchButton(new ColorButton(this)),
      m_docField(new QLineEdit(this)),
      m_customizeButton(new QPushButton(tr("Customize"), this)),
      m_revertButton(new QPushButton(tr("Revert to Built-in"), this)),
      m_message(messageLabel(this, QStringLiteral("presetSettingsMessage")))
{
    m_summary->setObjectName(QStringLiteral("presetSummary"));
    m_summary->setWordWrap(true);
    m_idField->setObjectName(QStringLiteral("presetIdField"));
    m_idField->setToolTip(tr("Its file's name in presets/"));
    m_nameField->setObjectName(QStringLiteral("presetNameField"));
    m_nameField->setToolTip(tr("What the editor shows; its id if empty"));
    m_classesField->setObjectName(QStringLiteral("presetClassesField"));
    m_classesField->setPlaceholderText(tr("e.g. enemy flying"));
    m_classesField->setToolTip(tr("Given to each top-level entity it creates"));
    m_swatchBox->setObjectName(QStringLiteral("presetSwatchBox"));
    m_swatchButton->setObjectName(QStringLiteral("presetSwatchButton"));
    m_docField->setObjectName(QStringLiteral("presetDocField"));
    m_docField->setToolTip(tr("Shown as its tooltip"));
    m_customizeButton->setObjectName(QStringLiteral("customizePresetButton"));
    m_customizeButton->setToolTip(tr("Copy it into the project, to change it there"));
    m_revertButton->setObjectName(QStringLiteral("revertPresetButton"));
    m_revertButton->setToolTip(tr("Remove the project's copy: Shoal's comes back"));

    auto* swatch = new QHBoxLayout;
    swatch->addWidget(m_swatchBox);
    swatch->addWidget(m_swatchButton);
    swatch->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_customizeButton);
    buttons->addWidget(m_revertButton);
    buttons->addStretch();
    auto* form = new QFormLayout;
    form->addRow(tr("Id"), m_idField);
    form->addRow(tr("Name"), m_nameField);
    form->addRow(tr("Classes"), m_classesField);
    form->addRow(tr("Color"), swatch);
    form->addRow(tr("Description"), m_docField);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_message);
    layout->addStretch();

    for (QLineEdit* field : {m_idField, m_nameField, m_classesField, m_docField}) {
        field->installEventFilter(this);
    }
    connect(m_idField, &QLineEdit::editingFinished, this, &PresetSettingsPanel::applyId);
    connect(m_nameField, &QLineEdit::editingFinished, this, &PresetSettingsPanel::applyName);
    connect(m_classesField, &QLineEdit::editingFinished, this, &PresetSettingsPanel::applyClasses);
    connect(m_docField, &QLineEdit::editingFinished, this, &PresetSettingsPanel::applyDoc);
    connect(m_swatchBox, &QCheckBox::clicked, this, [this](bool set) {
        setSwatch(set ? std::optional(m_swatchButton->color()) : std::nullopt);
    });
    connect(m_swatchButton, &ColorButton::colorChosen, this,
            [this](const core::Color& color) { setSwatch(color); });
    connect(m_customizeButton, &QPushButton::clicked, this, &PresetSettingsPanel::customize);
    connect(m_revertButton, &QPushButton::clicked, this, &PresetSettingsPanel::revert);
    setPreset(nullptr, {});
}

void PresetSettingsPanel::setPreset(core::ProjectDocument* document, const QString& presetId)
{
    if (m_document != document) {
        if (m_document) {
            disconnect(m_document, nullptr, this, nullptr);
        }
        m_document = document;
        if (m_document) {
            connect(m_document, &core::ProjectDocument::presetChanged, this,
                    &PresetSettingsPanel::refresh);
            connect(m_document, &core::ProjectDocument::presetRenamed, this,
                    [this](const QString& from, const QString& to) {
                        if (m_preset == from) {
                            m_preset = to;
                        }
                    });
        }
    }
    if (m_preset != presetId) {
        m_preset = presetId;
        showMessage({});
    }
    refresh();
}

QString PresetSettingsPanel::message() const
{
    return m_message->text();  // cleared when hidden
}

bool PresetSettingsPanel::applyId()
{
    if (!m_document || m_preset.isEmpty()) {
        return false;
    }
    const QString id = m_idField->text().trimmed();
    if (id == m_preset) {
        showMessage({});
        refresh();
        return true;
    }
    const core::Project& project = m_document->project();
    if (!core::isPresetIdAvailable(project, id)) {
        showMessage(core::isValidId(id)
                        ? tr("Another preset is already “%1”.").arg(id)
                        : tr("“%1” isn't a valid id: use letters, digits, _ and -, starting "
                             "with a letter or _.")
                              .arg(id));
        return false;
    }
    showMessage({});
    m_idField->setModified(false);
    m_document->commands().push(
        std::make_unique<core::RenamePresetCommand>(*m_document, m_preset, id));
    return true;
}

void PresetSettingsPanel::applyName()
{
    change(NAME_KEY, m_nameField->text().trimmed(), tr("Rename preset %1"));
}

bool PresetSettingsPanel::applyClasses()
{
    QString error;
    const std::optional<QStringList> classes =
        InspectorPanel::parseClasses(m_classesField->text(), &error);
    if (!classes) {
        showMessage(error);
        return false;
    }
    change(CLASSES_KEY, classes->isEmpty() ? QJsonValue() : QJsonArray::fromStringList(*classes),
           tr("Set the classes of preset %1"));
    return true;
}

void PresetSettingsPanel::setSwatch(std::optional<core::Color> swatch)
{
    if (swatch) {
        swatch->alpha = 255;  // a swatch is opaque
    }
    change(SWATCH_KEY, swatch ? QJsonValue(swatch->name()) : QJsonValue(), tr("Color preset %1"));
}

void PresetSettingsPanel::applyDoc()
{
    change(DOC_KEY, m_docField->text().trimmed(), tr("Describe preset %1"));
}

void PresetSettingsPanel::customize()
{
    if (!m_document || !core::isBuiltInPreset(m_document->project(), m_preset)) {
        return;
    }
    const core::Preset builtIn =
        core::builtInPresets(m_document->project().manifest.designResolution).value(m_preset);
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, m_preset, builtIn, tr("Customize preset %1").arg(m_preset)));
}

void PresetSettingsPanel::revert()
{
    if (!m_document || !m_document->project().presets.contains(m_preset) ||
        !core::builtInPresets().contains(m_preset)) {
        return;
    }
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, m_preset, std::nullopt, tr("Revert preset %1").arg(m_preset)));
}

bool PresetSettingsPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Escape drops what was typed.
    if (event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (auto* field = qobject_cast<QLineEdit*>(watched)) {
            showMessage({});
            field->setModified(false);
            refresh();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PresetSettingsPanel::refresh()
{
    const QMap<QString, core::Preset> presets =
        m_document ? core::availablePresets(m_document->project()) : QMap<QString, core::Preset>();
    const bool shown = presets.contains(m_preset);
    const bool builtIn = shown && core::isBuiltInPreset(m_document->project(), m_preset);
    const bool customized = shown && !builtIn && core::builtInPresets().contains(m_preset);
    const QJsonObject content = presets.value(m_preset).content;
    const core::PresetRead read =
        shown ? core::readPresetDefinition(m_preset, presets[m_preset]) : core::PresetRead{};

    m_summary->setText(
        !shown       ? tr("Select a preset to see its settings.")
        : builtIn    ? tr("Built in: customize it to change it.")
        : customized ? tr("%1, customizing a built-in preset.").arg(core::paths::preset(m_preset))
                     : core::paths::preset(m_preset));
    if (shown && !read.definition) {
        showMessage(tr("Can't read it: %1").arg(read.error));
    }
    const bool editable = shown && !builtIn;
    for (QWidget* field : std::initializer_list<QWidget*>{m_nameField, m_classesField, m_swatchBox,
                                                          m_swatchButton, m_docField}) {
        field->setEnabled(editable);
    }
    // A customized one keeps the built-in's id, which is what it customizes.
    m_idField->setEnabled(editable && !customized);
    m_customizeButton->setVisible(builtIn);
    m_revertButton->setVisible(customized);

    const auto show = [](QLineEdit* field, const QString& text) {
        if (!(field->hasFocus() && field->isModified())) {
            field->setText(text);
            field->setModified(false);
        }
    };
    show(m_idField, m_preset);
    show(m_nameField, content.value(NAME_KEY).toString());
    m_nameField->setPlaceholderText(m_preset);
    QStringList classes;
    for (const QJsonValue& name : content.value(CLASSES_KEY).toArray()) {
        classes << name.toString();
    }
    show(m_classesField, classes.join(u' '));
    show(m_docField, content.value(DOC_KEY).toString());
    const std::optional<core::Color> swatch =
        core::Color::fromName(content.value(SWATCH_KEY).toString());
    m_swatchBox->setChecked(swatch.has_value());
    m_swatchButton->setColor(swatch.value_or(core::Color{}));
}

void PresetSettingsPanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

void PresetSettingsPanel::change(const QString& key, const QJsonValue& value, const QString& text)
{
    if (!m_document || !m_document->project().presets.contains(m_preset)) {
        return;  // a built-in one is customized first
    }
    core::Preset preset = m_document->project().presets.value(m_preset);
    const bool empty = value.isNull() || (value.isString() && value.toString().isEmpty());
    if (empty ? !preset.content.contains(key) : preset.content.value(key) == value) {
        showMessage({});
        refresh();  // tidies what was typed
        return;
    }
    if (empty) {
        preset.content.remove(key);
    }
    else {
        preset.content.insert(key, value);
    }
    showMessage({});
    for (QLineEdit* field : {m_nameField, m_classesField, m_docField}) {
        field->setModified(false);
    }
    m_document->commands().push(std::make_unique<core::SetPresetCommand>(
        *m_document, m_preset, std::move(preset), text.arg(m_preset)));
}

}  // namespace shoal::editor
