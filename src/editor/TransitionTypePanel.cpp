#include "editor/TransitionTypePanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "core/TransitionTypeCommands.h"
#include "core/TransitionTypes.h"
#include "editor/FormFields.h"
#include "editor/Icons.h"

namespace shoal::editor {

using core::OptionType;

namespace {

const QString NAME = QStringLiteral("name");
const QString TYPE = QStringLiteral("type");
const QString DESCRIPTION = QStringLiteral("description");
const QString PREVIEW = QStringLiteral("preview");
const QString OPTIONS = QStringLiteral("options");
const QString DEFAULT = QStringLiteral("default");
const QString MIN = QStringLiteral("min");
const QString MAX = QStringLiteral("max");
const QString INTEGER = QStringLiteral("integer");
const QString CHOICES = QStringLiteral("choices");

QString optionTypeLabel(OptionType type)
{
    switch (type) {
        case OptionType::Time:
            return TransitionTypePanel::tr("Time, in milliseconds");
        case OptionType::Number:
            return TransitionTypePanel::tr("Number");
        case OptionType::String:
            return TransitionTypePanel::tr("String");
        case OptionType::Path:
            return TransitionTypePanel::tr("Path to a file");
        case OptionType::Color:
            return TransitionTypePanel::tr("Color");
    }
    return {};
}

/// Shows `text` in `field`, unless it is being typed in.
void showText(QLineEdit* field, const QString& text)
{
    if (!(field->hasFocus() && field->isModified())) {
        field->setText(text);
        field->setModified(false);
    }
}

/// A value as a field shows it: a number or a string as it is, choices joined by commas.
QString textOf(const QJsonValue& value)
{
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue& part : value.toArray()) {
            parts << part.toString();
        }
        return parts.join(QStringLiteral(", "));
    }
    return value.toString();
}

/// A number typed in a field, or undefined when it is empty; a string when it isn't one,
/// for the type's reading to refuse with why.
QJsonValue numberOf(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    bool ok = false;
    const double number = text.trimmed().toDouble(&ok);
    return ok ? QJsonValue(number) : QJsonValue(text.trimmed());
}

}  // namespace

TransitionTypePanel::TransitionTypePanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_customizeButton(new QPushButton(tr("Customize"), this)),
      m_revertButton(new QPushButton(tr("Revert to Built-in"), this)),
      m_idField(new QLineEdit(this)),
      m_nameField(new QLineEdit(this)),
      m_descriptionField(new QLineEdit(this)),
      m_previewField(new QLineEdit(this)),
      m_browsePreviewButton(new QToolButton(this)),
      m_optionList(new QListWidget(this)),
      m_addOptionButton(new QToolButton(this)),
      m_removeOptionButton(new QToolButton(this)),
      m_moveUpButton(new QToolButton(this)),
      m_moveDownButton(new QToolButton(this)),
      m_optionForm(new QWidget(this)),
      m_optionLayout(new QFormLayout(m_optionForm)),
      m_optionName(new QLineEdit(this)),
      m_optionType(new QComboBox(this)),
      m_optionDefault(new QLineEdit(this)),
      m_optionMin(new QLineEdit(this)),
      m_optionMax(new QLineEdit(this)),
      m_optionInteger(new QCheckBox(tr("Whole numbers only"), this)),
      m_optionChoices(new QLineEdit(this)),
      m_optionDescription(new QLineEdit(this)),
      m_message(messageLabel(this, QStringLiteral("transitionTypeMessage")))
{
    m_summary->setObjectName(QStringLiteral("transitionTypeSummary"));
    m_summary->setWordWrap(true);
    m_customizeButton->setObjectName(QStringLiteral("customizeTransitionTypeButton"));
    m_customizeButton->setToolTip(tr("Copy Shoal's type into the project, to change it there"));
    m_revertButton->setObjectName(QStringLiteral("revertTransitionTypeButton"));
    m_idField->setObjectName(QStringLiteral("transitionTypeIdField"));
    m_idField->setToolTip(tr("Its file's name, which effects name it by"));
    m_nameField->setObjectName(QStringLiteral("transitionTypeNameField"));
    m_descriptionField->setObjectName(QStringLiteral("transitionTypeDescriptionField"));
    m_previewField->setObjectName(QStringLiteral("transitionTypePreviewField"));
    m_previewField->setPlaceholderText(tr("A GIF showing it, beside its file"));
    m_browsePreviewButton->setText(QStringLiteral("…"));
    m_browsePreviewButton->setToolTip(tr("Choose a GIF"));
    auto* preview = new QHBoxLayout;
    preview->setContentsMargins({});
    preview->addWidget(m_previewField);
    preview->addWidget(m_browsePreviewButton);

    m_optionList->setObjectName(QStringLiteral("transitionOptionList"));
    m_optionList->setMaximumHeight(8 * m_optionList->fontMetrics().height());
    m_addOptionButton->setObjectName(QStringLiteral("addOptionButton"));
    m_addOptionButton->setIcon(icon(Icon::Add));
    m_addOptionButton->setToolTip(tr("Add an option"));
    m_removeOptionButton->setObjectName(QStringLiteral("removeOptionButton"));
    m_removeOptionButton->setIcon(icon(Icon::Remove));
    m_removeOptionButton->setToolTip(tr("Remove the option"));
    m_moveUpButton->setObjectName(QStringLiteral("moveOptionUpButton"));
    m_moveUpButton->setIcon(icon(Icon::MoveUp));
    m_moveUpButton->setToolTip(tr("Move the option up"));
    m_moveDownButton->setObjectName(QStringLiteral("moveOptionDownButton"));
    m_moveDownButton->setIcon(icon(Icon::MoveDown));
    m_moveDownButton->setToolTip(tr("Move the option down"));
    auto* optionButtons = new QHBoxLayout;
    for (QToolButton* button :
         {m_addOptionButton, m_removeOptionButton, m_moveUpButton, m_moveDownButton}) {
        optionButtons->addWidget(button);
    }
    optionButtons->addStretch();

    m_optionName->setObjectName(QStringLiteral("optionNameField"));
    m_optionName->setToolTip(tr("Its key in the effect's values, which the engine reads"));
    m_optionType->setObjectName(QStringLiteral("optionTypeBox"));
    for (const auto& [type, name] : core::enumNames(OptionType{})) {
        m_optionType->addItem(optionTypeLabel(type), QString::fromLatin1(name));
    }
    m_optionDefault->setObjectName(QStringLiteral("optionDefaultField"));
    m_optionDefault->setPlaceholderText(tr("None: unset until given one"));
    m_optionMin->setObjectName(QStringLiteral("optionMinField"));
    m_optionMin->setPlaceholderText(tr("None"));
    m_optionMax->setObjectName(QStringLiteral("optionMaxField"));
    m_optionMax->setPlaceholderText(tr("None"));
    m_optionInteger->setObjectName(QStringLiteral("optionIntegerBox"));
    m_optionChoices->setObjectName(QStringLiteral("optionChoicesField"));
    m_optionChoices->setPlaceholderText(tr("Any: or list them, separated by commas"));
    m_optionDescription->setObjectName(QStringLiteral("optionDescriptionField"));
    m_optionLayout->setContentsMargins({});
    m_optionLayout->addRow(tr("Name"), m_optionName);
    m_optionLayout->addRow(tr("Type"), m_optionType);
    m_optionLayout->addRow(tr("Default"), m_optionDefault);
    m_optionLayout->addRow(tr("Min"), m_optionMin);
    m_optionLayout->addRow(tr("Max"), m_optionMax);
    m_optionLayout->addRow(QString(), m_optionInteger);
    m_optionLayout->addRow(tr("Choices"), m_optionChoices);
    m_optionLayout->addRow(tr("Description"), m_optionDescription);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_customizeButton);
    buttons->addWidget(m_revertButton);
    buttons->addStretch();
    auto* form = new QFormLayout;
    form->addRow(tr("Id"), m_idField);
    form->addRow(tr("Name"), m_nameField);
    form->addRow(tr("Description"), m_descriptionField);
    form->addRow(tr("Preview"), preview);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->addWidget(m_summary);
    layout->addLayout(buttons);
    layout->addLayout(form);
    layout->addSpacing(layout->spacing());
    layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Options")), content));
    layout->addWidget(m_optionList);
    layout->addLayout(optionButtons);
    layout->addWidget(m_optionForm);
    layout->addWidget(m_message);
    layout->addStretch();
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins({});
    outer->addWidget(scroll);

    connect(m_customizeButton, &QPushButton::clicked, this, [this] {
        if (m_document && !isEditable() && core::builtInTransitionTypes().contains(m_type)) {
            m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
                *m_document, m_type, core::builtInTransitionTypes().value(m_type),
                tr("Customize transition type %1").arg(m_type)));
        }
    });
    connect(m_revertButton, &QPushButton::clicked, this, [this] {
        if (m_document && isEditable() && core::builtInTransitionTypes().contains(m_type)) {
            m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
                *m_document, m_type, std::nullopt, tr("Revert transition type %1").arg(m_type)));
        }
    });
    m_idField->installEventFilter(this);
    connect(m_idField, &QLineEdit::editingFinished, this, &TransitionTypePanel::applyId);
    connect(m_nameField, &QLineEdit::editingFinished, this, [this] {
        if (m_nameField->isModified()) {
            m_nameField->setModified(false);
            const QString name = m_nameField->text().trimmed();
            setKey(NAME, name.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(name));
        }
    });
    connect(m_descriptionField, &QLineEdit::editingFinished, this, [this] {
        if (m_descriptionField->isModified()) {
            m_descriptionField->setModified(false);
            const QString text = m_descriptionField->text().trimmed();
            setKey(DESCRIPTION,
                   text.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(text));
        }
    });
    connect(m_previewField, &QLineEdit::editingFinished, this, [this] {
        if (m_previewField->isModified()) {
            m_previewField->setModified(false);
            const QString text = m_previewField->text().trimmed();
            setKey(PREVIEW, text.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(text));
        }
    });
    connect(m_browsePreviewButton, &QToolButton::clicked, this,
            &TransitionTypePanel::browsePreview);
    connect(m_optionList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_option = row;
        showMessage({});
        showOption();
    });
    connect(m_addOptionButton, &QToolButton::clicked, this, &TransitionTypePanel::addOption);
    connect(m_removeOptionButton, &QToolButton::clicked, this, &TransitionTypePanel::removeOption);
    connect(m_moveUpButton, &QToolButton::clicked, this, [this] { moveOption(-1); });
    connect(m_moveDownButton, &QToolButton::clicked, this, [this] { moveOption(1); });
    for (QLineEdit* field : {m_optionName, m_optionDefault, m_optionMin, m_optionMax,
                             m_optionChoices, m_optionDescription}) {
        connect(field, &QLineEdit::editingFinished, this, [this, field] {
            if (field->isModified()) {
                field->setModified(false);
                applyOptionField(field);
            }
        });
    }
    connect(m_optionInteger, &QCheckBox::clicked, this,
            [this] { applyOptionField(m_optionInteger); });
    connect(m_optionType, &QComboBox::activated, this, &TransitionTypePanel::chooseOptionType);
    setDocument(nullptr);
}

void TransitionTypePanel::setDocument(core::ProjectDocument* document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document) {
        connect(m_document, &core::ProjectDocument::transitionTypeChanged, this,
                &TransitionTypePanel::refresh);
        connect(m_document, &core::ProjectDocument::transitionTypeRenamed, this,
                [this](const QString& from, const QString& to) {
                    if (m_type == from) {
                        m_type = to;
                    }
                });
    }
    m_type.clear();
    m_option = -1;
    showMessage({});
    refresh();
}

void TransitionTypePanel::setType(const QString& typeId)
{
    if (m_type != typeId) {
        m_type = typeId;
        m_option = -1;
        showMessage({});
    }
    refresh();
}

QString TransitionTypePanel::message() const
{
    return m_message->text();  // cleared when hidden
}

void TransitionTypePanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

bool TransitionTypePanel::isEditable() const
{
    return m_document && m_document->project().transitionTypes.contains(m_type);
}

QJsonObject TransitionTypePanel::content() const
{
    return m_document ? core::availableTransitionTypes(m_document->project()).value(m_type).content
                      : QJsonObject();
}

QJsonArray TransitionTypePanel::options() const
{
    return content().value(OPTIONS).toArray();
}

int TransitionTypePanel::currentOption() const
{
    return m_option >= 0 && m_option < options().size() ? m_option : -1;
}

bool TransitionTypePanel::eventFilter(QObject* watched, QEvent* event)
{
    // Escape drops what was typed.
    if (watched == m_idField && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        showMessage({});
        m_idField->setModified(false);
        refresh();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TransitionTypePanel::refresh()
{
    const bool shown = m_document && !m_type.isEmpty() &&
                       core::availableTransitionTypes(m_document->project()).contains(m_type);
    const bool editable = shown && isEditable();
    const bool builtIn = core::builtInTransitionTypes().contains(m_type);
    setEnabled(shown);
    m_customizeButton->setVisible(shown && builtIn && !editable);
    m_revertButton->setVisible(shown && builtIn && editable);
    for (QWidget* field :
         std::initializer_list<QWidget*>{m_nameField, m_descriptionField, m_previewField,
                                         m_browsePreviewButton, m_addOptionButton, m_optionForm}) {
        field->setEnabled(editable);
    }
    m_idField->setEnabled(editable && !builtIn);  // a customized one keeps the built-in's id
    if (!shown) {
        m_summary->setText(tr("Select a transition type to see it."));
        for (QLineEdit* field : {m_idField, m_nameField, m_descriptionField, m_previewField}) {
            field->clear();
        }
        m_optionList->clear();
        showOption();
        return;
    }

    const QJsonObject file = content();
    const core::TransitionTypeRead read =
        core::readTransitionType(m_type, core::TransitionType{file});
    QString summary =
        !editable ? tr("Built in: customize it to change it.")
        : builtIn ? tr("%1, customizing a built-in one.").arg(core::paths::transitionType(m_type))
                  : core::paths::transitionType(m_type);
    if (!read.definition) {
        summary += u'\n' + tr("Can't read it: %1").arg(read.error);
    }
    m_summary->setText(summary);
    showText(m_idField, m_type);
    showText(m_nameField, file.value(NAME).toString());
    m_nameField->setPlaceholderText(m_type);
    showText(m_descriptionField, file.value(DESCRIPTION).toString());
    showText(m_previewField, file.value(PREVIEW).toString());

    // The options by name, remade only when they change.
    const QJsonArray all = file.value(OPTIONS).toArray();
    QStringList names;
    for (const QJsonValue& option : all) {
        const QJsonObject object = option.toObject();
        names << QStringLiteral("%1 (%2)").arg(object.value(NAME).toString(),
                                               object.value(TYPE).toString());
    }
    bool same = m_optionList->count() == names.size();
    for (int i = 0; same && i < names.size(); ++i) {
        same = m_optionList->item(i)->text() == names[i];
    }
    const QSignalBlocker blocker(m_optionList);
    if (!same) {
        m_optionList->clear();
        m_optionList->addItems(names);
    }
    if (m_option >= m_optionList->count()) {
        m_option = m_optionList->count() - 1;
    }
    m_optionList->setCurrentRow(m_option);
    showOption();
}

void TransitionTypePanel::showOption()
{
    const int index = currentOption();
    const bool editable = isEditable();
    m_removeOptionButton->setEnabled(editable && index >= 0);
    m_moveUpButton->setEnabled(editable && index > 0);
    m_moveDownButton->setEnabled(editable && index >= 0 && index + 1 < options().size());
    m_optionForm->setVisible(index >= 0);
    if (index < 0) {
        return;
    }
    const QJsonObject option = options().at(index).toObject();
    const std::optional<OptionType> type =
        core::enumFromName<OptionType>(option.value(TYPE).toString());
    showText(m_optionName, option.value(NAME).toString());
    {
        const QSignalBlocker blocker(m_optionType);
        m_optionType->setCurrentIndex(type ? m_optionType->findData(core::enumName(*type)) : -1);
    }
    showText(m_optionDefault, textOf(option.value(DEFAULT)));
    showText(m_optionMin, textOf(option.value(MIN)));
    showText(m_optionMax, textOf(option.value(MAX)));
    m_optionInteger->setChecked(option.value(INTEGER).toBool());
    showText(m_optionChoices, textOf(option.value(CHOICES)));
    showText(m_optionDescription, option.value(DESCRIPTION).toString());
    const bool ranged = type == OptionType::Time || type == OptionType::Number;
    m_optionLayout->setRowVisible(m_optionMin, ranged);
    m_optionLayout->setRowVisible(m_optionMax, ranged);
    m_optionLayout->setRowVisible(m_optionInteger, type == OptionType::Number);
    m_optionLayout->setRowVisible(m_optionChoices, type == OptionType::String);
    m_optionDefault->setPlaceholderText(type == OptionType::Color
                                            ? tr("e.g. #000000; none: unset until given one")
                                            : tr("None: unset until given one"));
}

bool TransitionTypePanel::apply(const QJsonObject& changed, const QString& text)
{
    if (!isEditable()) {
        return false;
    }
    const core::TransitionTypeRead read =
        core::readTransitionType(m_type, core::TransitionType{changed});
    if (!read.definition) {
        showMessage(read.error);
        return false;
    }
    showMessage({});
    if (changed != content()) {
        m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
            *m_document, m_type, core::TransitionType{changed}, text));
    }
    return true;
}

void TransitionTypePanel::setKey(const QString& key, const QJsonValue& value)
{
    QJsonObject changed = content();
    if (value.isUndefined()) {
        changed.remove(key);
    }
    else {
        changed.insert(key, value);
    }
    apply(changed, tr("Edit transition type %1").arg(m_type));
}

void TransitionTypePanel::setOption(const QJsonObject& option, const QString& text)
{
    const int index = currentOption();
    if (index < 0) {
        return;
    }
    QJsonObject changed = content();
    QJsonArray all = options();
    all[index] = option;
    changed.insert(OPTIONS, all);
    apply(changed, text);
}

void TransitionTypePanel::applyId()
{
    if (!m_idField->isModified() || !isEditable()) {
        return;
    }
    m_idField->setModified(false);
    const QString to = m_idField->text().trimmed();
    if (to == m_type) {
        return;
    }
    if (!core::isTransitionTypeIdAvailable(m_document->project(), to, m_type)) {
        showMessage(core::isValidId(to)
                        ? tr("Another transition type is already called %1.").arg(to)
                        : tr("An id starts with a letter or an underscore, followed by letters, "
                             "digits, underscores or hyphens."));
        return;
    }
    showMessage({});
    m_document->commands().push(
        std::make_unique<core::RenameTransitionTypeCommand>(*m_document, m_type, to));
}

void TransitionTypePanel::applyOptionField(QObject* field)
{
    const int index = currentOption();
    if (index < 0) {
        return;
    }
    QJsonObject option = options().at(index).toObject();
    const auto set = [&](const QString& key, const QJsonValue& value) {
        if (value.isUndefined() || (value.isString() && value.toString().isEmpty())) {
            option.remove(key);
        }
        else {
            option.insert(key, value);
        }
    };
    const OptionType type =
        core::enumFromName<OptionType>(option.value(TYPE).toString()).value_or(OptionType::Number);
    if (field == m_optionName) {
        set(NAME, m_optionName->text().trimmed());
    }
    else if (field == m_optionDefault) {
        const QString text = m_optionDefault->text().trimmed();
        set(DEFAULT, type == OptionType::Time || type == OptionType::Number ? numberOf(text)
                                                                            : QJsonValue(text));
    }
    else if (field == m_optionMin) {
        set(MIN, numberOf(m_optionMin->text()));
    }
    else if (field == m_optionMax) {
        set(MAX, numberOf(m_optionMax->text()));
    }
    else if (field == m_optionInteger) {
        set(INTEGER,
            m_optionInteger->isChecked() ? QJsonValue(true) : QJsonValue(QJsonValue::Undefined));
    }
    else if (field == m_optionChoices) {
        QJsonArray choices;
        for (const QString& part : m_optionChoices->text().split(u',')) {
            if (!part.trimmed().isEmpty()) {
                choices.append(part.trimmed());
            }
        }
        set(CHOICES, choices.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(choices));
    }
    else if (field == m_optionDescription) {
        set(DESCRIPTION, m_optionDescription->text().trimmed());
    }
    setOption(
        option,
        tr("Edit option %1 of transition type %2").arg(option.value(NAME).toString(), m_type));
    refresh();  // what was refused shows as it is
}

void TransitionTypePanel::chooseOptionType(int index)
{
    const int current = currentOption();
    const std::optional<OptionType> type =
        core::enumFromName<OptionType>(m_optionType->itemData(index).toString());
    if (current < 0 || !type) {
        return;
    }
    QJsonObject option = options().at(current).toObject();
    option.insert(TYPE, core::enumName(*type));
    // Only what the new type has.
    if (*type != OptionType::Time && *type != OptionType::Number) {
        option.remove(MIN);
        option.remove(MAX);
    }
    if (*type != OptionType::Number) {
        option.remove(INTEGER);
    }
    if (*type != OptionType::String) {
        option.remove(CHOICES);
    }
    core::TransitionOption check;
    check.type = *type;
    if (option.contains(DEFAULT) && !core::fitsOption(check, option.value(DEFAULT))) {
        option.remove(DEFAULT);
    }
    setOption(
        option,
        tr("Edit option %1 of transition type %2").arg(option.value(NAME).toString(), m_type));
    refresh();
}

void TransitionTypePanel::addOption()
{
    if (!isEditable()) {
        return;
    }
    QJsonArray all = options();
    QStringList names;
    for (const QJsonValue& option : all) {
        names << option.toObject().value(NAME).toString();
    }
    QString name = QStringLiteral("option");
    for (int n = 2; names.contains(name); ++n) {
        name = QStringLiteral("option_%1").arg(n);
    }
    all.append(QJsonObject{{NAME, name}, {TYPE, core::enumName(OptionType::Number)}});
    QJsonObject changed = content();
    changed.insert(OPTIONS, all);
    if (apply(changed, tr("Add option %1 to transition type %2").arg(name, m_type))) {
        m_option = int(all.size()) - 1;
        refresh();
    }
}

void TransitionTypePanel::removeOption()
{
    const int index = currentOption();
    if (index < 0) {
        return;
    }
    QJsonArray all = options();
    const QString name = all.at(index).toObject().value(NAME).toString();
    all.removeAt(index);
    QJsonObject changed = content();
    if (all.isEmpty()) {
        changed.remove(OPTIONS);
    }
    else {
        changed.insert(OPTIONS, all);
    }
    apply(changed, tr("Remove option %1 of transition type %2").arg(name, m_type));
}

void TransitionTypePanel::moveOption(int by)
{
    const int index = currentOption();
    QJsonArray all = options();
    const int to = index + by;
    if (index < 0 || to < 0 || to >= all.size()) {
        return;
    }
    const QJsonValue moved = all.at(index);
    all.removeAt(index);
    all.insert(to, moved);
    QJsonObject changed = content();
    changed.insert(OPTIONS, all);
    if (apply(changed, tr("Move option %1 of transition type %2")
                           .arg(moved.toObject().value(NAME).toString(), m_type))) {
        m_option = to;
        refresh();
    }
}

void TransitionTypePanel::browsePreview()
{
    if (!isEditable()) {
        return;
    }
    // Beside the type's file, in the project's transitions folder.
    const QString project = m_document->directory();
    const QString folder =
        project.isEmpty()
            ? QString()
            : QDir(project).filePath(QFileInfo(core::paths::transitionType(m_type)).path());
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Choose a Preview"), folder, tr("GIF (*.gif)"));
    if (!file.isEmpty()) {
        setKey(PREVIEW, folder.isEmpty() ? QDir::fromNativeSeparators(file)
                                         : QDir(folder).relativeFilePath(file));
    }
}

}  // namespace shoal::editor
