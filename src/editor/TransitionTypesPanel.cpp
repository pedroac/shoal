#include "editor/TransitionTypesPanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/ProjectCodec.h"
#include "core/ProjectDocument.h"
#include "core/TransitionTypeCommands.h"
#include "core/TransitionTypes.h"
#include "editor/Icons.h"

namespace shoal::editor {

namespace {
enum Role { TYPE_ID = Qt::UserRole };
const QColor ERROR_COLOR(200, 60, 40);
}  // namespace

TransitionTypesPanel::TransitionTypesPanel(QWidget* parent)
    : QWidget(parent),
      m_list(new QListWidget(this)),
      m_newButton(new QToolButton(this)),
      m_duplicateButton(new QToolButton(this)),
      m_removeButton(new QToolButton(this))
{
    m_list->setObjectName(QStringLiteral("transitionTypeList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_newButton->setObjectName(QStringLiteral("newTransitionTypeButton"));
    m_newButton->setText(tr("New"));
    m_newButton->setIcon(icon(Icon::Add));
    m_newButton->setToolTip(tr("A new transition type of the project's, with no options yet"));
    m_duplicateButton->setObjectName(QStringLiteral("duplicateTransitionTypeButton"));
    m_duplicateButton->setText(tr("Duplicate"));
    m_duplicateButton->setToolTip(tr("Copy the type into a new one"));
    m_removeButton->setObjectName(QStringLiteral("removeTransitionTypeButton"));
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setIcon(icon(Icon::Remove));
    for (QToolButton* button : {m_newButton, m_duplicateButton, m_removeButton}) {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_newButton);
    buttons->addWidget(m_duplicateButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    connect(m_newButton, &QToolButton::clicked, this, &TransitionTypesPanel::newType);
    connect(m_duplicateButton, &QToolButton::clicked, this, &TransitionTypesPanel::duplicateType);
    connect(m_removeButton, &QToolButton::clicked, this, &TransitionTypesPanel::removeType);
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        updateButtons();
        emit currentTypeChanged(currentType());
    });
    setDocument(nullptr);
}

void TransitionTypesPanel::setDocument(core::ProjectDocument* document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document) {
        connect(m_document, &core::ProjectDocument::transitionTypeChanged, this,
                &TransitionTypesPanel::rebuild);
        // The current row follows a rename.
        connect(m_document, &core::ProjectDocument::transitionTypeRenamed, this,
                [this](const QString& from, const QString& to) {
                    if (QListWidgetItem* row = m_list->currentItem();
                        row && row->data(TYPE_ID).toString() == from) {
                        row->setData(TYPE_ID, to);
                    }
                });
    }
    setEnabled(m_document != nullptr);
    rebuild();
}

QListWidgetItem* TransitionTypesPanel::typeRow(const QString& typeId) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(TYPE_ID).toString() == typeId) {
            return m_list->item(i);
        }
    }
    return nullptr;
}

QString TransitionTypesPanel::currentType() const
{
    const QListWidgetItem* row = m_list->currentItem();
    return row ? row->data(TYPE_ID).toString() : QString();
}

void TransitionTypesPanel::setCurrentType(const QString& typeId)
{
    m_list->setCurrentItem(typeRow(typeId));
}

QString TransitionTypesPanel::newType()
{
    if (!m_document) {
        return {};
    }
    const QString id =
        core::uniqueTransitionTypeId(m_document->project(), QStringLiteral("transition"));
    core::TransitionType type{{{QStringLiteral("name"), tr("New Transition")}}};
    m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
        *m_document, id, type, tr("Add transition type %1").arg(id)));
    setCurrentType(id);
    return id;
}

QString TransitionTypesPanel::duplicateType()
{
    const QString from = currentType();
    if (!m_document || from.isEmpty()) {
        return {};
    }
    core::TransitionType type = core::availableTransitionTypes(m_document->project()).value(from);
    const QString id = core::uniqueTransitionTypeId(m_document->project(), from);
    const core::TransitionTypeRead read = core::readTransitionType(from, type);
    type.content.insert(QStringLiteral("name"),
                        tr("%1 Copy").arg(read.definition ? read.definition->name : from));
    // A preview stays beside the type it was drawn for.
    type.content.remove(QStringLiteral("preview"));
    m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
        *m_document, id, type, tr("Duplicate transition type %1").arg(from)));
    setCurrentType(id);
    return id;
}

void TransitionTypesPanel::removeType()
{
    const QString id = currentType();
    if (!m_document || !m_document->project().transitionTypes.contains(id)) {
        return;  // a built-in one stays
    }
    const bool builtIn = core::builtInTransitionTypes().contains(id);
    m_document->commands().push(std::make_unique<core::SetTransitionTypeCommand>(
        *m_document, id, std::nullopt,
        builtIn ? tr("Revert transition type %1").arg(id)
                : tr("Remove transition type %1").arg(id)));
}

void TransitionTypesPanel::updateButtons()
{
    const QString id = currentType();
    m_newButton->setEnabled(m_document != nullptr);
    m_duplicateButton->setEnabled(!id.isEmpty());
    m_removeButton->setEnabled(m_document && m_document->project().transitionTypes.contains(id));
    const bool builtIn = core::builtInTransitionTypes().contains(id);
    m_removeButton->setText(builtIn ? tr("Revert") : tr("Remove"));
    m_removeButton->setToolTip(builtIn ? tr("Go back to Shoal's own type")
                                       : tr("Remove the project's type"));
}

void TransitionTypesPanel::rebuild()
{
    const QString current = currentType();
    {
        const QSignalBlocker blocker(m_list);  // the current one comes back below
        m_list->clear();
    }
    if (!m_document) {
        if (!current.isEmpty()) {
            emit currentTypeChanged({});
        }
        updateButtons();
        return;
    }
    const core::Project& project = m_document->project();
    const QMap<QString, core::TransitionType> types = core::availableTransitionTypes(project);
    std::vector<std::pair<QString, QListWidgetItem*>> rows;
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        const core::TransitionTypeRead read = core::readTransitionType(it.key(), *it);
        auto* row = new QListWidgetItem;
        row->setData(TYPE_ID, it.key());
        const bool own = project.transitionTypes.contains(it.key());
        const bool builtIn = core::builtInTransitionTypes().contains(it.key());
        const QString origin =
            !own ? tr("built in")
            : builtIn
                ? tr("%1, customizing a built-in one").arg(core::paths::transitionType(it.key()))
                : core::paths::transitionType(it.key());
        if (read.definition) {
            row->setText(own && builtIn ? tr("%1 (customized)").arg(read.definition->name)
                                        : read.definition->name);
            QStringList tips{QStringLiteral("<b>%1</b> (%2)").arg(it.key(), origin)};
            if (!read.definition->description.isEmpty()) {
                tips << read.definition->description.toHtmlEscaped();
            }
            row->setToolTip(tips.join(QStringLiteral("<br>")));
        }
        else {
            row->setText(tr("%1 (can't read)").arg(it.key()));
            row->setForeground(ERROR_COLOR);
            row->setIcon(icon(Icon::Error));
            row->setToolTip(QStringLiteral("%1: %2").arg(origin, read.error.toHtmlEscaped()));
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
        m_list->setCurrentItem(typeRow(current));
    }
    updateButtons();
    if (currentType() != current) {
        emit currentTypeChanged(currentType());
    }
}

}  // namespace shoal::editor
