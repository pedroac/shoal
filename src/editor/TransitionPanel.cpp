#include "editor/TransitionPanel.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMovie>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "core/SceneTree.h"
#include "editor/ColorButton.h"
#include "editor/FlowScene.h"
#include "editor/FormFields.h"
#include "editor/Icons.h"

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

QString modeLabel(core::TransitionMode mode)
{
    switch (mode) {
        case core::TransitionMode::Replace:
            return TransitionPanel::tr("Replace the scene");
        case core::TransitionMode::Push:
            return TransitionPanel::tr("Push a scene on top");
        case core::TransitionMode::PushModifier:
            return TransitionPanel::tr("Run a modifier");
        case core::TransitionMode::Pop:
            return TransitionPanel::tr("Pop back to the scene underneath");
    }
    return {};
}

/// Gives `box` these items, (text, data), remaking them only when they change, since the
/// box may be in its own signal's handler; then selects the one whose data is `current`,
/// adding it, marked as missing, if it isn't one of them.
void setItems(QComboBox* box, const QList<std::pair<QString, QString>>& items,
              const QString& current)
{
    const QSignalBlocker blocker(box);
    bool same = box->count() >= items.size();
    for (qsizetype i = 0; same && i < items.size(); ++i) {
        same = box->itemText(int(i)) == items[i].first && box->itemData(int(i)) == items[i].second;
    }
    if (!same) {
        box->clear();
        for (const auto& [text, data] : items) {
            box->addItem(text, data);
        }
    }
    while (box->count() > items.size()) {
        box->removeItem(box->count() - 1);
    }
    int index = box->findData(current);
    if (index < 0 && !current.isEmpty()) {
        box->addItem(TransitionPanel::tr("%1 (missing)").arg(current), current);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

/// The ids of a scene's entities that have one, depth first; only its marks if `marks`.
QList<std::pair<QString, QString>> entityIds(const core::Project& project, const QString& sceneId,
                                             bool marks)
{
    QList<std::pair<QString, QString>> ids;
    const auto scene = project.scenes.constFind(sceneId);
    if (scene == project.scenes.cend()) {
        return ids;
    }
    core::forEachEntity(*scene, [&](const core::Entity& entity, const core::EntityPath&) {
        if (!entity.id.isEmpty() && (!marks || core::kindOf(entity) == QStringLiteral("mark"))) {
            ids.append({entity.id, entity.id});
        }
    });
    return ids;
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
      m_idField(new QLineEdit(this)),
      m_modeBox(new QComboBox(this)),
      m_toBox(new QComboBox(this)),
      m_underlyingBox(new QComboBox(this)),
      m_modifierBox(new QComboBox(this)),
      m_route(new QFormLayout),
      m_eventField(new QLineEdit(this)),
      m_requiresField(new QLineEdit(this)),
      m_stateList(new QListWidget(this)),
      m_newStateField(new QLineEdit(this)),
      m_carried(new QGridLayout),
      m_carryEntityBox(new QComboBox(this)),
      m_carryMarkBox(new QComboBox(this)),
      m_carryButton(new QToolButton(this)),
      m_carriedEntities(new QWidget(this)),
      m_message(messageLabel(this, QStringLiteral("transitionMessage"))),
      m_typeBox(new QComboBox(this)),
      m_description(new QLabel(this)),
      m_preview(new QLabel(this)),
      m_durationBox(numberBox(this, QStringLiteral("effectDurationBox"), MAX_DURATION_MS, 0, 50)),
      m_easingBox(new QComboBox(this)),
      m_options(new QFormLayout)
{
    m_summary->setObjectName(QStringLiteral("transitionSummary"));
    m_summary->setWordWrap(true);
    m_idField->setObjectName(QStringLiteral("transitionIdField"));
    m_modeBox->setObjectName(QStringLiteral("transitionModeBox"));
    m_modeBox->setToolTip(tr("How the transition changes the stack of scenes"));
    for (const auto& [mode, name] : core::enumNames(core::TransitionMode{})) {
        m_modeBox->addItem(modeLabel(mode), QString::fromLatin1(name));
    }
    m_toBox->setObjectName(QStringLiteral("transitionToBox"));
    m_underlyingBox->setObjectName(QStringLiteral("transitionUnderlyingBox"));
    m_underlyingBox->setToolTip(tr("What the scene underneath does while the other is on top"));
    for (const auto& [underlying, name] : core::enumNames(core::Underlying{})) {
        m_underlyingBox->addItem(labelOf(QString::fromLatin1(name)), QString::fromLatin1(name));
    }
    m_modifierBox->setObjectName(QStringLiteral("transitionModifierBox"));
    m_route->addRow(tr("Id"), m_idField);
    m_route->addRow(tr("Mode"), m_modeBox);
    m_route->addRow(tr("To"), m_toBox);
    m_route->addRow(tr("Underneath"), m_underlyingBox);
    m_route->addRow(tr("Modifier"), m_modifierBox);

    m_eventField->setObjectName(QStringLiteral("transitionEventField"));
    m_eventField->setPlaceholderText(tr("None yet"));
    m_eventField->setToolTip(tr("The name of the event the engine fires to take the transition"));
    m_requiresField->setObjectName(QStringLiteral("transitionRequiresField"));
    m_requiresField->setPlaceholderText(tr("e.g. has_key, boss_defeated"));
    m_requiresField->setToolTip(
        tr("The conditions the engine checks first, by name, separated by commas"));
    auto* trigger = new QFormLayout;
    trigger->addRow(tr("Event"), m_eventField);
    trigger->addRow(tr("Requires"), m_requiresField);

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
    auto* effect = new QFormLayout;
    effect->addRow(tr("Type"), m_typeBox);
    effect->addRow(QString(), m_description);
    effect->addRow(QString(), m_preview);
    effect->addRow(tr("Duration"), m_durationBox);
    effect->addRow(tr("Easing"), m_easingBox);

    m_stateList->setObjectName(QStringLiteral("transitionStateList"));
    m_stateList->setToolTip(
        tr("The state keys whose values carry over, as the flow declares them"));
    m_stateList->setMaximumHeight(6 * m_stateList->fontMetrics().height());
    m_newStateField->setObjectName(QStringLiteral("newStateKeyField"));
    m_newStateField->setPlaceholderText(tr("Declare a key and carry it"));
    m_carryEntityBox->setObjectName(QStringLiteral("carryEntityBox"));
    m_carryEntityBox->setToolTip(tr("An entity of this scene to carry into the next"));
    m_carryMarkBox->setObjectName(QStringLiteral("carryMarkBox"));
    m_carryMarkBox->setToolTip(tr("The mark in the next scene it lands on"));
    m_carryButton->setObjectName(QStringLiteral("carryButton"));
    m_carryButton->setIcon(icon(Icon::Add));
    m_carryButton->setToolTip(tr("Carry the entity onto the mark"));
    auto* carryRow = new QHBoxLayout;
    carryRow->addWidget(m_carryEntityBox, 1);
    carryRow->addWidget(new QLabel(QStringLiteral("→"), this));
    carryRow->addWidget(m_carryMarkBox, 1);
    carryRow->addWidget(m_carryButton);
    auto* entities = new QVBoxLayout(m_carriedEntities);
    entities->setContentsMargins({});
    entities->addWidget(new QLabel(tr("Entities, each onto a mark:"), this));
    entities->addLayout(m_carried);
    entities->addLayout(carryRow);

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    const auto heading = [&](const QString& text) {
        auto* label = new QLabel(QStringLiteral("<b>%1</b>").arg(text), content);
        layout->addSpacing(layout->spacing());
        layout->addWidget(label);
    };
    layout->addWidget(m_summary);
    layout->addLayout(m_route);
    heading(tr("Trigger"));
    layout->addLayout(trigger);
    heading(tr("Effect"));
    layout->addLayout(effect);
    layout->addLayout(m_options);
    heading(tr("Carries"));
    layout->addWidget(new QLabel(tr("State:"), content));
    layout->addWidget(m_stateList);
    layout->addWidget(m_newStateField);
    layout->addWidget(m_carriedEntities);
    layout->addWidget(m_message);
    layout->addStretch();
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins({});
    outer->addWidget(scroll);

    m_idField->installEventFilter(this);
    connect(m_idField, &QLineEdit::editingFinished, this, &TransitionPanel::applyId);
    connect(m_modeBox, &QComboBox::activated, this, &TransitionPanel::chooseMode);
    connect(m_toBox, &QComboBox::activated, this, [this] {
        if (const std::optional<core::Transition> current = transition()) {
            core::Transition changed = *current;
            changed.to = m_toBox->currentData().toString();
            apply(changed, tr("Set where transition %1 goes").arg(current->id));
        }
    });
    connect(m_underlyingBox, &QComboBox::activated, this, [this] {
        if (const std::optional<core::Transition> current = transition()) {
            core::Transition changed = *current;
            changed.underlying =
                core::enumFromName<core::Underlying>(m_underlyingBox->currentData().toString())
                    .value_or(core::Underlying::Paused);
            apply(changed, tr("Set what is underneath transition %1").arg(current->id));
        }
    });
    connect(m_modifierBox, &QComboBox::activated, this, [this] {
        if (const std::optional<core::Transition> current = transition()) {
            core::Transition changed = *current;
            changed.modifier = m_modifierBox->currentData().toString();
            apply(changed, tr("Set the modifier of transition %1").arg(current->id));
        }
    });
    connect(m_eventField, &QLineEdit::editingFinished, this, &TransitionPanel::applyTrigger);
    connect(m_requiresField, &QLineEdit::editingFinished, this, &TransitionPanel::applyTrigger);
    connect(m_typeBox, &QComboBox::activated, this, &TransitionPanel::chooseType);
    connect(m_durationBox, &QDoubleSpinBox::valueChanged, this, &TransitionPanel::applyTiming);
    connect(m_easingBox, &QComboBox::activated, this, &TransitionPanel::applyTiming);
    connect(m_stateList, &QListWidget::itemChanged, this, &TransitionPanel::applyState);
    connect(m_newStateField, &QLineEdit::returnPressed, this, &TransitionPanel::declareState);
    connect(m_carryButton, &QToolButton::clicked, this, [this] {
        const QString entity = m_carryEntityBox->currentData().toString();
        const QString mark = m_carryMarkBox->currentData().toString();
        const std::optional<core::Transition> current = transition();
        if (!current || entity.isEmpty() || mark.isEmpty()) {
            return;
        }
        core::Transition changed = *current;
        changed.persist.entities.insert(entity, mark);
        apply(changed, tr("Carry %1 in transition %2").arg(entity, current->id));
    });
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
    const std::optional<core::Transition> current = transition();
    return current ? current->effect : std::nullopt;
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
    refreshTransition(transition);
    refreshCarried(transition);

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
        // Unset with no default: 0, or as near as its range allows.
        showValue(box, value.isDouble() ? value.toDouble()
                                        : std::clamp(0.0, box->minimum(), box->maximum()));
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

std::optional<core::Transition> TransitionPanel::transition() const
{
    const QString id = transitionId();
    return id.isEmpty() ? std::nullopt : std::optional(*m_flow->document().findTransition(id));
}

QString TransitionPanel::message() const
{
    return m_message->text();  // cleared when hidden
}

void TransitionPanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

bool TransitionPanel::eventFilter(QObject* watched, QEvent* event)
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

void TransitionPanel::refreshTransition(const core::Transition* transition)
{
    if (!(m_idField->hasFocus() && m_idField->isModified())) {
        m_idField->setText(transition ? transition->id : QString());
        m_idField->setModified(false);
    }
    const core::Project* project = m_flow ? &m_flow->document().project() : nullptr;
    const auto from = transition ? project->scenes.constFind(transition->from)
                                 : QMap<QString, core::Scene>::const_iterator();
    const bool hasFrom = transition && from != project->scenes.cend();
    const QStringList modifiers = hasFrom ? from->modifiers.keys() : QStringList();

    {
        const QSignalBlocker blocker(m_modeBox);
        m_modeBox->setCurrentIndex(
            transition ? m_modeBox->findData(core::enumName(transition->mode)) : 0);
    }
    // A modifier to run is one of the scene's.
    if (auto* model = qobject_cast<QStandardItemModel*>(m_modeBox->model())) {
        const int index = m_modeBox->findData(core::enumName(core::TransitionMode::PushModifier));
        model->item(index)->setEnabled(
            !modifiers.isEmpty() ||
            (transition && transition->mode == core::TransitionMode::PushModifier));
    }

    QList<std::pair<QString, QString>> scenes;
    for (const QString& sceneId : project ? project->scenes.keys() : QStringList()) {
        scenes.append({sceneId, sceneId});
    }
    setItems(m_toBox, scenes, transition ? transition->to : QString());
    {
        const QSignalBlocker blocker(m_underlyingBox);
        m_underlyingBox->setCurrentIndex(m_underlyingBox->findData(
            core::enumName(transition ? transition->underlying : core::Underlying::Paused)));
    }
    QList<std::pair<QString, QString>> modifierItems;
    for (const QString& modifier : modifiers) {
        modifierItems.append({modifier, modifier});
    }
    setItems(m_modifierBox, modifierItems, transition ? transition->modifier : QString());

    const core::TransitionMode mode = transition ? transition->mode : core::TransitionMode::Replace;
    const bool toScene =
        mode == core::TransitionMode::Replace || mode == core::TransitionMode::Push;
    m_route->setRowVisible(m_toBox, toScene);
    m_route->setRowVisible(m_underlyingBox, mode == core::TransitionMode::Push);
    m_route->setRowVisible(m_modifierBox, mode == core::TransitionMode::PushModifier);

    if (!(m_eventField->hasFocus() && m_eventField->isModified())) {
        m_eventField->setText(transition ? transition->trigger.event : QString());
        m_eventField->setModified(false);
    }
    if (!(m_requiresField->hasFocus() && m_requiresField->isModified())) {
        m_requiresField->setText(
            transition ? transition->trigger.requirements.join(QStringLiteral(", ")) : QString());
        m_requiresField->setModified(false);
    }
}

void TransitionPanel::refreshCarried(const core::Transition* transition)
{
    const core::Project* project = m_flow ? &m_flow->document().project() : nullptr;

    // The flow's keys, and any the transition carries that it doesn't declare. The rows are
    // only remade when the keys change: checking one is in its own signal's handler.
    QStringList keys;
    QMap<QString, QString> docs;
    for (const core::StateKey& key :
         project ? project->flow.state : std::vector<core::StateKey>{}) {
        keys << key.key;
        docs.insert(key.key, key.doc);
    }
    const QStringList carried = transition ? transition->persist.state : QStringList();
    for (const QString& key : carried) {
        if (!keys.contains(key)) {
            keys << key;
        }
    }
    const QSignalBlocker blocker(m_stateList);
    bool same = m_stateList->count() == keys.size();
    for (int i = 0; same && i < keys.size(); ++i) {
        same = m_stateList->item(i)->data(Qt::UserRole).toString() == keys[i];
    }
    if (!same) {
        m_stateList->clear();
        for (const QString& key : std::as_const(keys)) {
            auto* item = new QListWidgetItem(
                docs.contains(key) ? key : tr("%1 (not declared)").arg(key), m_stateList);
            item->setData(Qt::UserRole, key);
            item->setToolTip(docs.value(key));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        }
    }
    for (int i = 0; i < m_stateList->count(); ++i) {
        QListWidgetItem* item = m_stateList->item(i);
        item->setCheckState(carried.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked
                                                                                  : Qt::Unchecked);
    }

    // Entities go onto marks in the destination, so only where there is one.
    const bool toScene = transition && (transition->mode == core::TransitionMode::Replace ||
                                        transition->mode == core::TransitionMode::Push);
    m_carriedEntities->setVisible(toScene);
    const QMap<QString, QString> entities =
        transition ? transition->persist.entities : QMap<QString, QString>();
    const QList<std::pair<QString, QString>> marks =
        toScene ? entityIds(*project, transition->to, true) : QList<std::pair<QString, QString>>{};
    QStringList markIds;
    for (const auto& mark : marks) {
        markIds << mark.second;
    }
    const QString carriedFor = QStringList(entities.keys()).join(u',') + u'|' + markIds.join(u',');
    if (carriedFor != m_carriedFor) {
        m_carriedFor = carriedFor;
        // Later, as a box may be in its own signal's handler.
        while (QLayoutItem* item = m_carried->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
        m_markBoxes.clear();
        int row = 0;
        for (auto it = entities.cbegin(); it != entities.cend(); ++it, ++row) {
            const QString entity = it.key();
            auto* markBox = new QComboBox(this);
            markBox->setObjectName(QStringLiteral("markBox_%1").arg(entity));
            auto* remove = new QToolButton(this);
            remove->setIcon(icon(Icon::Remove));
            remove->setToolTip(tr("Stop carrying %1").arg(entity));
            m_carried->addWidget(new QLabel(entity, this), row, 0);
            m_carried->addWidget(new QLabel(QStringLiteral("→"), this), row, 1);
            m_carried->addWidget(markBox, row, 2);
            m_carried->addWidget(remove, row, 3);
            m_carried->setColumnStretch(2, 1);
            m_markBoxes.insert(entity, markBox);
            connect(markBox, &QComboBox::activated, this, [this, entity, markBox] {
                if (const std::optional<core::Transition> current = this->transition()) {
                    core::Transition changed = *current;
                    changed.persist.entities.insert(entity, markBox->currentData().toString());
                    apply(changed, tr("Carry %1 in transition %2").arg(entity, current->id));
                }
            });
            connect(remove, &QToolButton::clicked, this, [this, entity] {
                if (const std::optional<core::Transition> current = this->transition()) {
                    core::Transition changed = *current;
                    changed.persist.entities.remove(entity);
                    apply(changed,
                          tr("Stop carrying %1 in transition %2").arg(entity, current->id));
                }
            });
        }
    }
    for (auto it = entities.cbegin(); it != entities.cend(); ++it) {
        if (QComboBox* box = m_markBoxes.value(it.key())) {
            setItems(box, marks, it.value());
        }
    }

    QList<std::pair<QString, QString>> candidates;
    if (toScene) {
        for (const auto& entity : entityIds(*project, transition->from, false)) {
            if (!entities.contains(entity.second)) {
                candidates.append(entity);
            }
        }
    }
    const auto keep = [](QComboBox* box, const QList<std::pair<QString, QString>>& items) {
        const QString shown = box->currentData().toString();
        const bool listed = std::any_of(items.begin(), items.end(),
                                        [&](const auto& item) { return item.second == shown; });
        return listed ? shown : items.isEmpty() ? QString() : items.front().second;
    };
    setItems(m_carryEntityBox, candidates, keep(m_carryEntityBox, candidates));
    setItems(m_carryMarkBox, marks, keep(m_carryMarkBox, marks));
    m_carryButton->setEnabled(!candidates.isEmpty() && !marks.isEmpty());
}

bool TransitionPanel::apply(const core::Transition& changed, const QString& text)
{
    const QString id = transitionId();
    if (id.isEmpty()) {
        return false;
    }
    showMessage({});
    return m_flow->setTransition(id, changed, text);
}

void TransitionPanel::applyId()
{
    const std::optional<core::Transition> current = transition();
    if (!current || !m_idField->isModified()) {
        return;
    }
    core::Transition changed = *current;
    changed.id = m_idField->text().trimmed();
    m_idField->setModified(false);
    if (changed.id == current->id) {
        refresh();
        return;
    }
    if (!apply(changed, tr("Rename transition %1 to %2").arg(current->id, changed.id))) {
        showMessage(FlowScene::transitionRenameRefusal(changed.id));
        m_idField->setText(changed.id);  // kept, to be fixed
    }
}

void TransitionPanel::chooseMode(int index)
{
    const std::optional<core::Transition> current = transition();
    const std::optional<core::TransitionMode> mode =
        core::enumFromName<core::TransitionMode>(m_modeBox->itemData(index).toString());
    if (!current || !mode) {
        return;
    }
    core::Transition changed = *current;
    changed.mode = *mode;
    // What the mode needs, and nothing it doesn't: a destination for replace and push (the
    // scene itself until another is chosen), a modifier of the scene's for push_modifier.
    if (*mode == core::TransitionMode::Replace || *mode == core::TransitionMode::Push) {
        if (changed.to.isEmpty()) {
            changed.to = changed.from;
        }
    }
    else {
        changed.to.clear();
    }
    if (*mode == core::TransitionMode::PushModifier) {
        if (changed.modifier.isEmpty()) {
            const auto& scenes = m_flow->document().project().scenes;
            const auto from = scenes.constFind(changed.from);
            if (from == scenes.cend() || from->modifiers.isEmpty()) {
                refresh();
                return;
            }
            changed.modifier = from->modifiers.firstKey();
        }
    }
    else {
        changed.modifier.clear();
    }
    apply(changed, tr("Set the mode of transition %1").arg(current->id));
    refresh();
}

void TransitionPanel::applyTrigger()
{
    const std::optional<core::Transition> current = transition();
    if (!current) {
        return;
    }
    core::Transition changed = *current;
    changed.trigger.event = m_eventField->text().trimmed();
    QStringList requirements;
    for (const QString& part : m_requiresField->text().split(u',')) {
        const QString requirement = part.trimmed();
        if (!requirement.isEmpty() && !requirements.contains(requirement)) {
            requirements << requirement;
        }
    }
    changed.trigger.requirements = requirements;
    m_eventField->setModified(false);
    m_requiresField->setModified(false);
    apply(changed, tr("Set the trigger of transition %1").arg(current->id));
    refresh();  // the requirements as they are kept
}

void TransitionPanel::applyState(QListWidgetItem*)
{
    const std::optional<core::Transition> current = transition();
    if (!current) {
        return;
    }
    core::Transition changed = *current;
    changed.persist.state.clear();
    for (int i = 0; i < m_stateList->count(); ++i) {
        if (m_stateList->item(i)->checkState() == Qt::Checked) {
            changed.persist.state << m_stateList->item(i)->data(Qt::UserRole).toString();
        }
    }
    apply(changed, tr("Set the state transition %1 carries").arg(current->id));
}

void TransitionPanel::declareState()
{
    const std::optional<core::Transition> current = transition();
    const QString key = m_newStateField->text().trimmed();
    if (!current || key.isEmpty()) {
        return;
    }
    const std::vector<core::StateKey>& keys = m_flow->document().project().flow.state;
    const bool declared = std::any_of(keys.begin(), keys.end(),
                                      [&](const core::StateKey& k) { return k.key == key; });
    if (declared) {
        // Already declared: just carried.
        core::Transition changed = *current;
        if (!changed.persist.state.contains(key)) {
            changed.persist.state << key;
            apply(changed, tr("Set the state transition %1 carries").arg(current->id));
        }
    }
    else if (!m_flow->declareStateKey(current->id, key)) {
        return;
    }
    m_newStateField->clear();
}

}  // namespace shoal::editor
