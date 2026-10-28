#include "editor/InspectorPanel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStringListModel>
#include <QVBoxLayout>
#include <cmath>

#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/FormFields.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

const QRegularExpression SEPARATORS(QStringLiteral("[\\s,]+"));

/// Where the class being typed starts: after the last separator before the cursor, and
/// after its dot.
qsizetype tokenStart(const QString& text, qsizetype cursor)
{
    qsizetype start = cursor;
    while (start > 0 && !text[start - 1].isSpace() && text[start - 1] != u',') {
        --start;
    }
    if (start < cursor && text[start] == u'.') {
        ++start;
    }
    return start;
}

}  // namespace

InspectorPanel::InspectorPanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_idField(new QLineEdit(this)),
      m_classesField(new QLineEdit(this)),
      m_activeBox(new QCheckBox(tr("Active"), this)),
      m_visibleBox(new QCheckBox(tr("Visible"), this)),
      m_angleBox(numberBox(this, QStringLiteral("angleBox"), 360, 2, 15)),
      m_scaleXBox(numberBox(this, QStringLiteral("scaleXBox"), 1000, 3, 0.1)),
      m_scaleYBox(numberBox(this, QStringLiteral("scaleYBox"), 1000, 3, 0.1)),
      m_widthBox(numberBox(this, QStringLiteral("widthBox"), MAX_SIZE, 2, 1)),
      m_heightBox(numberBox(this, QStringLiteral("heightBox"), MAX_SIZE, 2, 1)),
      m_flipXBox(new QCheckBox(tr("Horizontal"), this)),
      m_flipYBox(new QCheckBox(tr("Vertical"), this)),
      m_message(messageLabel(this, QStringLiteral("inspectorMessage"))),
      m_form(new QFormLayout),
      m_scaleRow(new QWidget(this)),
      m_sizeRow(new QWidget(this)),
      m_completer(new QCompleter(this)),
      m_classNames(new QStringListModel(this))
{
    m_summary->setObjectName(QStringLiteral("inspectorSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_idField->setObjectName(QStringLiteral("idField"));
    m_idField->setPlaceholderText(tr("none: nothing refers to it"));
    m_classesField->setObjectName(QStringLiteral("classesField"));
    m_classesField->setPlaceholderText(tr("e.g. enemy flying"));
    m_angleBox->setSuffix(QStringLiteral("°"));
    m_angleBox->setToolTip(tr("Clockwise, in degrees, about the entity's position"));
    m_scaleXBox->setPrefix(QStringLiteral("x "));
    m_scaleYBox->setPrefix(QStringLiteral("y "));
    m_widthBox->setMinimum(1);
    m_heightBox->setMinimum(1);
    m_widthBox->setPrefix(QStringLiteral("w "));
    m_heightBox->setPrefix(QStringLiteral("h "));
    m_sizeRow->setToolTip(
        tr("The nine-slice's borders keep their size; the edges and the "
           "middle stretch"));
    m_flipXBox->setObjectName(QStringLiteral("flipXBox"));
    m_flipYBox->setObjectName(QStringLiteral("flipYBox"));
    m_flipXBox->setToolTip(tr("Mirrors the entity's own drawing, not its children"));
    m_flipYBox->setToolTip(m_flipXBox->toolTip());

    m_completer->setModel(m_classNames);
    m_completer->setWidget(m_classesField);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    m_form->addRow(tr("Id"), m_idField);
    m_form->addRow(tr("Classes"), m_classesField);
    m_form->addRow(QString(), m_activeBox);
    m_form->addRow(QString(), m_visibleBox);
    m_form->addRow(tr("Angle"), m_angleBox);
    auto* size = new QHBoxLayout(m_sizeRow);
    size->setContentsMargins({});
    size->addWidget(m_widthBox);
    size->addWidget(m_heightBox);
    m_form->addRow(tr("Size"), m_sizeRow);
    auto* scale = new QHBoxLayout(m_scaleRow);
    scale->setContentsMargins({});
    scale->addWidget(m_scaleXBox);
    scale->addWidget(m_scaleYBox);
    m_form->addRow(tr("Scale"), m_scaleRow);
    auto* flip = new QHBoxLayout;
    flip->addWidget(m_flipXBox);
    flip->addWidget(m_flipYBox);
    flip->addStretch();
    m_form->addRow(tr("Flip"), flip);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(m_form);
    layout->addWidget(m_message);
    layout->addStretch();

    m_activeBox->setToolTip(
        tr("An inactive entity is authored but not present until an action "
           "activates it"));
    m_idField->installEventFilter(this);
    m_classesField->installEventFilter(this);
    connect(m_idField, &QLineEdit::editingFinished, this, &InspectorPanel::applyId);
    // The completer's popup takes focus with Qt::PopupFocusReason, which doesn't finish
    // editing.
    connect(m_classesField, &QLineEdit::editingFinished, this, &InspectorPanel::applyClasses);
    connect(m_classesField, &QLineEdit::textEdited, this, &InspectorPanel::completeClass);
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this,
            &InspectorPanel::insertClass);
    connect(m_activeBox, &QCheckBox::clicked, this, &InspectorPanel::setActive);
    connect(m_visibleBox, &QCheckBox::clicked, this, &InspectorPanel::setVisibleFlag);
    connect(m_angleBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyAngle);
    connect(m_scaleXBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyScale);
    connect(m_scaleYBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyScale);
    connect(m_widthBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applySize);
    connect(m_heightBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applySize);
    connect(m_flipXBox, &QCheckBox::clicked, this, &InspectorPanel::setFlipX);
    connect(m_flipYBox, &QCheckBox::clicked, this, &InspectorPanel::setFlipY);
    setCanvas(nullptr);
}

void InspectorPanel::setCanvas(SceneCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_canvas = canvas;
    m_document = canvas ? &canvas->document() : nullptr;
    if (m_canvas) {
        connect(m_canvas, &SceneCanvas::contentChanged, this, &InspectorPanel::refresh);
        connect(m_canvas, &QGraphicsScene::selectionChanged, this, [this] {
            showMessage({});
            refresh();
        });
        connect(m_document, &core::ProjectDocument::classesChanged, this,
                &InspectorPanel::refreshClasses);
    }
    showMessage({});
    refreshClasses();
    refresh();
}

QString InspectorPanel::message() const
{
    return m_message->isVisible() ? m_message->text() : QString();
}

std::optional<QStringList> InspectorPanel::parseClasses(const QString& text, QString* error)
{
    QStringList classes;
    for (QString name : text.split(SEPARATORS, Qt::SkipEmptyParts)) {
        if (name.startsWith(u'.')) {
            name.remove(0, 1);
        }
        if (!core::isValidId(name)) {
            if (error) {
                *error = tr("“%1” isn't a valid class: use letters, digits, _ and -, "
                            "starting with a letter or _.")
                             .arg(name);
            }
            return std::nullopt;
        }
        if (!classes.contains(name)) {
            classes << name;
        }
    }
    return classes;
}

bool InspectorPanel::applyId()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    const QString id = m_idField->text().trimmed();
    if (id == entity->id) {
        showMessage({});
        m_idField->setText(id);
        return true;
    }
    const QString sceneId = m_canvas->sceneId();
    if (id.isEmpty()) {
        // Transitions would be left naming nothing.
        const QStringList naming = m_document->transitionsNamingEntity(sceneId, entity->id);
        if (!naming.isEmpty()) {
            showMessage(tr("The transitions %1 refer to “%2”; it needs an id.")
                            .arg(naming.join(QStringLiteral(", ")), entity->id));
            return false;
        }
    }
    else if (!core::isValidId(id)) {
        showMessage(tr("“%1” isn't a valid id: use letters, digits, _ and -, starting with a "
                       "letter or _.")
                        .arg(id));
        return false;
    }
    else if (!m_document->isEntityIdAvailable(sceneId, id, path)) {
        showMessage(tr("Another entity of this scene is already “%1”.").arg(id));
        return false;
    }
    Entity changed = *entity;
    changed.id = id;
    const QString text = id.isEmpty() ? tr("Remove the id of %1").arg(entity->id)
                         : entity->id.isEmpty()
                             ? tr("Name %1 %2").arg(core::enumName(entity->type), id)
                             : tr("Rename %1 to %2").arg(entity->id, id);
    push(path, std::move(changed), text);
    return true;
}

bool InspectorPanel::applyClasses()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    QString error;
    const std::optional<QStringList> classes = parseClasses(m_classesField->text(), &error);
    if (!classes) {
        showMessage(error);
        return false;
    }
    if (*classes == entity->classes) {
        showMessage({});
        refresh();  // tidies what was typed
        return true;
    }
    Entity changed = *entity;
    changed.classes = *classes;
    push(path, std::move(changed), tr("Set the classes of %1").arg(core::describe(*entity)));
    return true;
}

void InspectorPanel::applyAngle()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || m_angleBox->value() == entity->transform.rot) {
        return;
    }
    Entity changed = *entity;
    changed.transform.rot = m_angleBox->value();
    push(path, std::move(changed), tr("Rotate %1").arg(core::describe(*entity)));
}

bool InspectorPanel::applyScale()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    const core::Point scale{m_scaleXBox->value(), m_scaleYBox->value()};
    if (std::abs(scale.x) < core::MIN_SCALE || std::abs(scale.y) < core::MIN_SCALE) {
        showMessage(tr("A scale can't be zero."));
        refresh();
        return false;
    }
    if (scale == entity->transform.scale) {
        showMessage({});
        return true;
    }
    Entity changed = *entity;
    changed.transform.scale = scale;
    push(path, std::move(changed), tr("Scale %1").arg(core::describe(*entity)));
    return true;
}

void InspectorPanel::applySize()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && entity->type == core::EntityType::NineSlice) {
        m_canvas->commitSize(path, QSizeF(m_widthBox->value(), m_heightBox->value()));
    }
}

bool InspectorPanel::showsScale() const
{
    return m_form->isRowVisible(m_scaleRow);
}

bool InspectorPanel::showsSize() const
{
    return m_form->isRowVisible(m_sizeRow);
}

bool InspectorPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Escape drops what was typed.
    if ((watched == m_idField || watched == m_classesField) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
        !m_completer->popup()->isVisible()) {
        showMessage({});
        static_cast<QLineEdit*>(watched)->setModified(false);
        refresh();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void InspectorPanel::refresh()
{
    const QList<EntityPath> paths = m_canvas ? m_canvas->selectedPaths() : QList<EntityPath>();
    const Entity* entity = selected();
    for (QWidget* field : std::initializer_list<QWidget*>{
             m_idField, m_classesField, m_activeBox, m_visibleBox, m_angleBox, m_scaleXBox,
             m_scaleYBox, m_widthBox, m_heightBox, m_flipXBox, m_flipYBox}) {
        field->setEnabled(entity != nullptr);
    }
    if (!entity) {
        m_summary->setText(!m_canvas ? tr("Open a scene to inspect its entities.")
                           : paths.isEmpty()
                               ? tr("Nothing selected.")
                               : tr("%n entities selected.", nullptr, int(paths.size())));
        m_idField->clear();
        m_classesField->clear();
        m_classesField->setToolTip({});
        m_activeBox->setChecked(false);
        m_visibleBox->setChecked(false);
        showValue(m_angleBox, 0);
        showValue(m_scaleXBox, 1);
        showValue(m_scaleYBox, 1);
        m_flipXBox->setChecked(false);
        m_flipYBox->setChecked(false);
        m_form->setRowVisible(m_sizeRow, false);
        m_form->setRowVisible(m_scaleRow, true);
        return;
    }

    QString summary = core::enumName(entity->type);
    if (entity->sprite) {
        summary += QStringLiteral(" · %1 · %2").arg(entity->sprite->sheet, entity->sprite->path);
    }
    else if (entity->animation) {
        summary +=
            QStringLiteral(" · %1 · %2").arg(entity->animation->sheet, entity->animation->name);
    }
    m_summary->setText(summary);
    // Keep what is being typed; anything else shows the model.
    if (!(m_idField->hasFocus() && m_idField->isModified())) {
        m_idField->setText(entity->id);
        m_idField->setModified(false);
    }
    if (!(m_classesField->hasFocus() && m_classesField->isModified())) {
        m_classesField->setText(entity->classes.join(u' '));
        m_classesField->setModified(false);
    }
    m_activeBox->setChecked(entity->active);
    m_visibleBox->setChecked(entity->visible);
    showValue(m_angleBox, entity->transform.rot);
    showValue(m_scaleXBox, entity->transform.scale.x);
    showValue(m_scaleYBox, entity->transform.scale.y);
    m_flipXBox->setChecked(entity->transform.flipX);
    m_flipYBox->setChecked(entity->transform.flipY);
    const QSizeF size = m_canvas->nineSliceSize(*entity);
    m_form->setRowVisible(m_sizeRow, !size.isEmpty());
    m_form->setRowVisible(m_scaleRow,
                          size.isEmpty() || entity->transform.scale != core::Point{1, 1});
    if (!size.isEmpty()) {
        showValue(m_widthBox, size.width());
        showValue(m_heightBox, size.height());
    }

    // Each class's declaration, as a tooltip.
    const auto& registry = m_document->project().classes;
    QStringList lines;
    for (const QString& name : entity->classes) {
        if (!registry || !registry->classes.contains(name)) {
            lines << tr("<b>.%1</b>: not declared").arg(name);
            continue;
        }
        const QString doc = registry->classes.value(name).doc;
        lines << (doc.isEmpty() ? QStringLiteral("<b>.%1</b>").arg(name)
                                : QStringLiteral("<b>.%1</b>: %2").arg(name, doc.toHtmlEscaped()));
    }
    m_classesField->setToolTip(lines.join(QStringLiteral("<br>")));
}

void InspectorPanel::refreshClasses()
{
    // Declared or in use: the class registry.
    QStringList names;
    if (m_document) {
        names = m_document->classUses().keys();
        if (const auto& registry = m_document->project().classes) {
            for (const QString& name : registry->classes.keys()) {
                if (!names.contains(name)) {
                    names << name;
                }
            }
        }
        names.sort();
    }
    m_classNames->setStringList(names);
    refresh();
}

void InspectorPanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

void InspectorPanel::setActive(bool active)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->active != active) {
        Entity changed = *entity;
        changed.active = active;
        push(path, std::move(changed),
             (active ? tr("Activate %1") : tr("Deactivate %1")).arg(core::describe(*entity)));
    }
}

void InspectorPanel::setVisibleFlag(bool visible)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->visible != visible) {
        Entity changed = *entity;
        changed.visible = visible;
        push(path, std::move(changed),
             (visible ? tr("Show %1") : tr("Hide %1")).arg(core::describe(*entity)));
    }
}

void InspectorPanel::setFlipX(bool flip)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->transform.flipX != flip) {
        Entity changed = *entity;
        changed.transform.flipX = flip;
        push(path, std::move(changed),
             (flip ? tr("Flip %1 horizontally") : tr("Unflip %1 horizontally"))
                 .arg(core::describe(*entity)));
    }
}

void InspectorPanel::setFlipY(bool flip)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->transform.flipY != flip) {
        Entity changed = *entity;
        changed.transform.flipY = flip;
        push(path, std::move(changed),
             (flip ? tr("Flip %1 vertically") : tr("Unflip %1 vertically"))
                 .arg(core::describe(*entity)));
    }
}

const Entity* InspectorPanel::selected(EntityPath* path) const
{
    if (!m_canvas || !m_canvas->scene()) {
        return nullptr;
    }
    const QList<EntityPath> paths = m_canvas->selectedPaths();
    if (paths.size() != 1) {
        return nullptr;
    }
    if (path) {
        *path = paths.first();
    }
    return core::entityAt(*m_canvas->scene(), paths.first());
}

void InspectorPanel::push(const EntityPath& path, Entity entity, const QString& text)
{
    showMessage({});
    m_idField->setModified(false);
    m_classesField->setModified(false);
    m_document->commands().push(std::make_unique<core::EditEntitiesCommand>(
        *m_document, m_canvas->sceneId(), QList<std::pair<EntityPath, Entity>>{{path, entity}},
        text));
}

void InspectorPanel::completeClass(const QString& text)
{
    const qsizetype cursor = m_classesField->cursorPosition();
    const QString prefix = text.mid(tokenStart(text, cursor), cursor - tokenStart(text, cursor));
    if (prefix.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() > 0) {
        m_completer->complete();
    }
    else {
        m_completer->popup()->hide();
    }
}

void InspectorPanel::insertClass(const QString& name)
{
    const QString text = m_classesField->text();
    const qsizetype cursor = m_classesField->cursorPosition();
    const qsizetype start = tokenStart(text, cursor);
    const QString completed = text.left(start) + name + u' ';
    m_classesField->setText(completed + text.mid(cursor).trimmed());
    m_classesField->setCursorPosition(int(completed.size()));
    m_classesField->setModified(true);
}

}  // namespace shoal::editor
