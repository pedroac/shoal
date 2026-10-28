#include "editor/InspectorPanel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStringListModel>
#include <QVBoxLayout>

#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

const QRegularExpression SEPARATORS(QStringLiteral("[\\s,]+"));

/// An entity's name in a command's text: its id, or its type.
QString describe(const Entity& entity)
{
    return entity.id.isEmpty() ? core::enumName(entity.type) : entity.id;
}

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
      m_message(new QLabel(this)),
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
    m_message->setObjectName(QStringLiteral("inspectorMessage"));
    m_message->setWordWrap(true);
    m_message->setStyleSheet(QStringLiteral("color: #c83c28"));
    m_message->hide();

    m_completer->setModel(m_classNames);
    m_completer->setWidget(m_classesField);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    auto* form = new QFormLayout;
    form->addRow(tr("Id"), m_idField);
    form->addRow(tr("Classes"), m_classesField);
    form->addRow(QString(), m_activeBox);
    form->addRow(QString(), m_visibleBox);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(form);
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
    push(path, std::move(changed), tr("Set the classes of %1").arg(describe(*entity)));
    return true;
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
    for (QWidget* field :
         {static_cast<QWidget*>(m_idField), static_cast<QWidget*>(m_classesField),
          static_cast<QWidget*>(m_activeBox), static_cast<QWidget*>(m_visibleBox)}) {
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
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

void InspectorPanel::setActive(bool active)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->active != active) {
        Entity changed = *entity;
        changed.active = active;
        push(path, std::move(changed),
             (active ? tr("Activate %1") : tr("Deactivate %1")).arg(describe(*entity)));
    }
}

void InspectorPanel::setVisibleFlag(bool visible)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->visible != visible) {
        Entity changed = *entity;
        changed.visible = visible;
        push(path, std::move(changed),
             (visible ? tr("Show %1") : tr("Hide %1")).arg(describe(*entity)));
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
