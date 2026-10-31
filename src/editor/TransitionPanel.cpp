#include "editor/TransitionPanel.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMovie>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "editor/ColorButton.h"
#include "editor/FlowScene.h"
#include "editor/FormFields.h"

namespace shoal::editor {

using core::Effect;
using core::OptionType;
using core::TransitionOption;
using core::TransitionTypeDefinition;

namespace {

constexpr double MAX_DURATION_MS = 600000;  ///< Ten minutes: longer than any transition.
constexpr double MAX_NUMBER = 1000000;
constexpr int PREVIEW_WIDTH = 240;

QString easingLabel(core::Easing easing)
{
    switch (easing) {
        case core::Easing::Linear:
            return TransitionPanel::tr("Linear");
        case core::Easing::EaseIn:
            return TransitionPanel::tr("Ease in");
        case core::Easing::EaseOut:
            return TransitionPanel::tr("Ease out");
        case core::Easing::EaseInOut:
            return TransitionPanel::tr("Ease in and out");
    }
    return {};
}

/// An option's name as a label: "block_size" is "Block size".
QString labelOf(const QString& name)
{
    QString label = name;
    label.replace(u'_', u' ');
    if (!label.isEmpty()) {
        label[0] = label[0].toUpper();
    }
    return label;
}

/// Selects the item whose data is `name`; one it doesn't list is added after the first
/// `known` items, marked as unknown, in place of the previous such item.
void showName(QComboBox* box, int known, const QString& name)
{
    const QSignalBlocker blocker(box);
    while (box->count() > known) {
        box->removeItem(known);
    }
    int index = box->findData(name);
    if (index < 0) {
        box->addItem(TransitionPanel::tr("%1 (unknown)").arg(name), name);
        index = known;
    }
    box->setCurrentIndex(index);
}

}  // namespace

TransitionPanel::TransitionPanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_typeBox(new QComboBox(this)),
      m_description(new QLabel(this)),
      m_preview(new QLabel(this)),
      m_durationBox(numberBox(this, QStringLiteral("effectDurationBox"), MAX_DURATION_MS, 0, 50)),
      m_easingBox(new QComboBox(this)),
      m_options(new QFormLayout)
{
    m_summary->setObjectName(QStringLiteral("transitionSummary"));
    m_summary->setWordWrap(true);
    m_typeBox->setObjectName(QStringLiteral("effectTypeBox"));
    m_typeBox->setToolTip(tr("What the screen does as the scene changes; the engine draws it"));
    m_description->setObjectName(QStringLiteral("effectDescription"));
    m_description->setWordWrap(true);
    m_description->setForegroundRole(QPalette::PlaceholderText);
    m_preview->setObjectName(QStringLiteral("effectPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->hide();
    m_durationBox->setMinimum(0);
    m_durationBox->setSuffix(tr(" ms"));
    m_easingBox->setObjectName(QStringLiteral("effectEasingBox"));
    for (const auto& [easing, name] : core::enumNames(core::Easing{})) {
        // Linear is the default, written as no easing.
        m_easingBox->addItem(easingLabel(easing), easing == core::Easing::Linear
                                                      ? QString()
                                                      : QString::fromLatin1(name));
    }

    auto* form = new QFormLayout;
    form->addRow(tr("Effect"), m_typeBox);
    form->addRow(QString(), m_description);
    form->addRow(QString(), m_preview);
    form->addRow(tr("Duration"), m_durationBox);
    form->addRow(tr("Easing"), m_easingBox);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(form);
    layout->addLayout(m_options);
    layout->addStretch();

    connect(m_typeBox, &QComboBox::activated, this, &TransitionPanel::chooseType);
    connect(m_durationBox, &QDoubleSpinBox::valueChanged, this, &TransitionPanel::applyTiming);
    connect(m_easingBox, &QComboBox::activated, this, &TransitionPanel::applyTiming);
    setFlowScene(nullptr);
}

void TransitionPanel::setFlowScene(FlowScene* flow)
{
    if (m_flow) {
        disconnect(m_flow, nullptr, this, nullptr);
        disconnect(&m_flow->document(), nullptr, this, nullptr);
    }
    m_flow = flow;
    if (m_flow) {
        connect(m_flow, &QGraphicsScene::selectionChanged, this, &TransitionPanel::refresh);
        connect(&m_flow->document(), &core::ProjectDocument::changed, this,
                &TransitionPanel::refresh);
    }
    refresh();
}

QString TransitionPanel::transitionId() const
{
    if (!m_flow || !m_flow->selectedSceneIds().isEmpty()) {
        return {};
    }
    const QStringList ids = m_flow->selectedTransitionIds();
    return ids.size() == 1 && m_flow->document().findTransition(ids.first()) ? ids.first()
                                                                             : QString();
}

QWidget* TransitionPanel::optionField(const QString& name) const
{
    return m_fields.value(name);
}

std::optional<Effect> TransitionPanel::effect() const
{
    const QString id = transitionId();
    return id.isEmpty() ? std::nullopt : m_flow->document().findTransition(id)->effect;
}

void TransitionPanel::refresh()
{
    const QString id = transitionId();
    setEnabled(!id.isEmpty());
    const core::Transition* transition =
        id.isEmpty() ? nullptr : m_flow->document().findTransition(id);
    const std::optional<Effect> current = transition ? transition->effect : std::nullopt;

    if (!transition) {
        m_summary->setText(tr("Select a transition to see its effect."));
    }
    else {
        const QString to = transition->mode == core::TransitionMode::Pop
                               ? tr("the scene underneath")
                           : transition->mode == core::TransitionMode::PushModifier
                               ? tr("modifier %1").arg(transition->modifier)
                               : transition->to;
        m_summary->setText(tr("<b>%1</b><br>%2 → %3, %4")
                               .arg(id.toHtmlEscaped(), transition->from.toHtmlEscaped(),
                                    to.toHtmlEscaped(), core::enumName(transition->mode)));
    }

    // The types to choose from, by name; remade only when they change, since the box may
    // be in its own signal's handler.
    const core::Project* project = m_flow ? &m_flow->document().project() : nullptr;
    const QMap<QString, core::TransitionType> types =
        project ? core::availableTransitionTypes(*project) : core::builtInTransitionTypes();
    std::vector<std::pair<QString, QString>> listed;  // name, id
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        const core::TransitionTypeRead read = core::readTransitionType(it.key(), *it);
        listed.emplace_back(
            read.definition ? read.definition->name : tr("%1 (can't read)").arg(it.key()),
            it.key());
    }
    std::sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) {
        return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
    });
    const int known = 1 + int(listed.size());
    bool same = m_typeBox->count() >= known;
    for (int i = 0; same && i < int(listed.size()); ++i) {
        same = m_typeBox->itemText(i + 1) == listed[size_t(i)].first &&
               m_typeBox->itemData(i + 1).toString() == listed[size_t(i)].second;
    }
    if (!same) {
        const QSignalBlocker blocker(m_typeBox);
        m_typeBox->clear();
        m_typeBox->addItem(tr("None"), QString());
        for (const auto& [name, typeId] : listed) {
            m_typeBox->addItem(name, typeId);
        }
    }
    showName(m_typeBox, known, current ? current->type : QString());

    const auto type = current ? types.constFind(current->type) : types.cend();
    const std::optional<TransitionTypeDefinition> definition =
        type != types.cend() ? core::readTransitionType(type.key(), *type).definition
                             : std::nullopt;
    if (!current) {
        m_description->setText(tr("The scene changes at once."));
    }
    else if (type == types.cend()) {
        m_description->setText(tr("The project has no transition type “%1”; its values are "
                                  "kept as they are.")
                                   .arg(current->type));
    }
    else if (!definition) {
        m_description->setText(core::readTransitionType(type.key(), *type).error);
    }
    else {
        m_description->setText(definition->description);
    }
    m_description->setVisible(!m_description->text().isEmpty());

    // The preview, next to the type's file: Shoal's, or the project's transitions folder.
    QString previewFile;
    if (definition && !definition->preview.isEmpty()) {
        const QString dir =
            project && project->transitionTypes.contains(current->type)
                ? QDir(m_flow->document().directory())
                      .filePath(QFileInfo(core::paths::transitionType(current->type)).path())
                : core::builtInTransitionTypesDir();
        previewFile = QDir(dir).filePath(definition->preview);
    }
    if (previewFile != m_previewFile) {
        m_previewFile = previewFile;
        delete m_preview->movie();
        m_preview->setMovie(nullptr);
        if (!previewFile.isEmpty() && QFileInfo::exists(previewFile)) {
            auto* movie = new QMovie(previewFile, QByteArray(), m_preview);
            movie->jumpToFrame(0);
            const QSize size = movie->currentImage().size();
            if (size.width() > PREVIEW_WIDTH) {
                movie->setScaledSize(
                    size.scaled(PREVIEW_WIDTH, PREVIEW_WIDTH, Qt::KeepAspectRatio));
            }
            m_preview->setMovie(movie);
            movie->start();
        }
    }
    m_preview->setVisible(m_preview->movie() != nullptr);

    m_durationBox->setEnabled(current.has_value());
    m_easingBox->setEnabled(current.has_value());
    showValue(m_durationBox, current ? current->durationMs : 0);
    showName(m_easingBox, int(core::enumNames(core::Easing{}).size()),
             current ? current->easing : QString());

    const QString optionsFor =
        definition ? type.key() + u'\n' + QString::fromUtf8(QJsonDocument(type->content).toJson())
                   : QString();
    if (optionsFor != m_optionsFor) {
        m_optionsFor = optionsFor;
        buildOptions(definition ? &*definition : nullptr);
    }
    if (definition) {
        for (const TransitionOption& option : definition->options) {
            showOption(option, core::optionValue(*current, option));
        }
    }
}

void TransitionPanel::buildOptions(const TransitionTypeDefinition* type)
{
    // Later, as a field may be in its own signal's handler.
    while (m_options->rowCount() > 0) {
        const QFormLayout::TakeRowResult row = m_options->takeRow(0);
        for (QLayoutItem* item : {row.labelItem, row.fieldItem}) {
            if (item && item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
    m_fields.clear();
    m_optionsType = type ? std::optional(*type) : std::nullopt;
    if (!type) {
        return;
    }
    for (const TransitionOption& option : type->options) {
        QWidget* field = makeField(option);
        field->setObjectName(QStringLiteral("option_%1").arg(option.name));
        field->setToolTip(option.description);
        m_fields.insert(option.name, field);
        m_options->addRow(labelOf(option.name), field);
    }
}

QWidget* TransitionPanel::makeField(const TransitionOption& option)
{
    const QString name = option.name;
    switch (option.type) {
        case OptionType::Time:
        case OptionType::Number: {
            const bool time = option.type == OptionType::Time;
            const bool whole = time || option.integer;
            const double minimum = option.minimum.value_or(time ? 0 : -MAX_NUMBER);
            const double maximum = option.maximum.value_or(time ? MAX_DURATION_MS : MAX_NUMBER);
            const double step = time ? 50 : whole || maximum - minimum > 10 ? 1 : 0.05;
            QDoubleSpinBox* box = numberBox(this, QString(), MAX_NUMBER, whole ? 0 : 3, step);
            box->setRange(minimum, maximum);
            if (time) {
                box->setSuffix(tr(" ms"));
            }
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, name, whole](double value) {
                setOption(name,
                          whole ? QJsonValue(qint64(std::llround(value))) : QJsonValue(value));
            });
            return box;
        }
        case OptionType::String:
            if (!option.choices.isEmpty()) {
                auto* box = new QComboBox(this);
                box->addItems(option.choices);
                connect(box, &QComboBox::activated, this,
                        [this, name, box] { setOption(name, box->currentText()); });
                return box;
            }
            [[fallthrough]];
        case OptionType::Path: {
            auto* edit = new QLineEdit(this);
            const auto apply = [this, name, edit] {
                const QString text = edit->text().trimmed();
                setOption(name,
                          text.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(text));
                edit->setModified(false);
            };
            connect(edit, &QLineEdit::editingFinished, this, apply);
            if (option.type == OptionType::String) {
                return edit;
            }
            edit->setPlaceholderText(tr("A file, relative to the project"));
            auto* row = new QWidget(this);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins({});
            layout->addWidget(edit);
            auto* browse = new QToolButton(row);
            browse->setText(QStringLiteral("…"));
            browse->setToolTip(tr("Choose a file"));
            layout->addWidget(browse);
            connect(browse, &QToolButton::clicked, this, [this, name] { browsePath(name); });
            return row;
        }
        case OptionType::Color: {
            auto* button = new ColorButton(this);
            connect(button, &ColorButton::colorChosen, this,
                    [this, name](const core::Color& color) { setOption(name, color.name()); });
            return button;
        }
    }
    return new QWidget(this);
}

void TransitionPanel::showOption(const TransitionOption& option, const QJsonValue& value)
{
    QWidget* field = m_fields.value(option.name);
    if (auto* box = qobject_cast<QDoubleSpinBox*>(field)) {
        showValue(box, value.isDouble() ? value.toDouble() : box->minimum());
    }
    else if (auto* choices = qobject_cast<QComboBox*>(field)) {
        const QSignalBlocker blocker(choices);
        choices->setCurrentIndex(choices->findText(value.toString()));
    }
    else if (auto* button = qobject_cast<ColorButton*>(field)) {
        button->setColor(core::Color::fromName(value.toString()).value_or(core::Color{0, 0, 0}));
    }
    else if (field) {
        // A string's own, or a path's, beside its browse button.
        auto* edit = qobject_cast<QLineEdit*>(field);
        edit = edit ? edit : field->findChild<QLineEdit*>();
        if (edit && !(edit->hasFocus() && edit->isModified())) {
            edit->setText(value.toString());
            edit->setModified(false);
        }
    }
}

void TransitionPanel::setEffect(const std::optional<Effect>& effect)
{
    const QString id = transitionId();
    if (!id.isEmpty()) {
        m_flow->setTransitionEffect(id, effect);
    }
}

void TransitionPanel::chooseType(int index)
{
    const QString typeId = m_typeBox->itemData(index).toString();
    const std::optional<Effect> current = effect();
    if (typeId.isEmpty()) {
        setEffect(std::nullopt);
    }
    else if (const auto types = core::availableTransitionTypes(m_flow->document().project());
             types.contains(typeId)) {
        const std::optional<TransitionTypeDefinition> type =
            core::readTransitionType(typeId, types.value(typeId)).definition;
        if (type) {
            std::optional<TransitionTypeDefinition> old;
            if (current && types.contains(current->type)) {
                old =
                    core::readTransitionType(current->type, types.value(current->type)).definition;
            }
            setEffect(current ? core::withEffectType(*current, typeId, *type, old ? &*old : nullptr)
                              : core::makeEffect(typeId, *type));
        }
    }
    refresh();  // a type that can't be read, or the unknown one chosen again, changes nothing
}

void TransitionPanel::applyTiming()
{
    std::optional<Effect> changed = effect();
    if (!changed) {
        return;
    }
    changed->durationMs = int(m_durationBox->value());
    changed->easing = m_easingBox->currentData().toString();
    setEffect(changed);
}

void TransitionPanel::setOption(const QString& name, const QJsonValue& value)
{
    std::optional<Effect> changed = effect();
    if (!changed) {
        return;
    }
    if (value.isUndefined()) {
        changed->params.remove(name);
    }
    else {
        changed->params.insert(name, value);
    }
    setEffect(changed);
}

void TransitionPanel::browsePath(const QString& name)
{
    const QString directory = m_flow ? m_flow->document().directory() : QString();
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a File"), directory);
    if (file.isEmpty()) {
        return;
    }
    // Relative to the project, once it has a folder, and with forward slashes.
    setOption(name, directory.isEmpty() ? QDir::fromNativeSeparators(file)
                                        : QDir(directory).relativeFilePath(file));
}

}  // namespace shoal::editor
