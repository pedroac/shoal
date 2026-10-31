#include "core/TransitionTypeCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"
#include "core/TransitionTypes.h"

namespace shoal::core {

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::core::TransitionTypeCommands", text);
}
}  // namespace

SetTransitionTypeCommand::SetTransitionTypeCommand(ProjectDocument& document, QString id,
                                                   std::optional<TransitionType> type, QString text)
    : m_document(document), m_id(std::move(id)), m_after(std::move(type)), m_text(std::move(text))
{
    const auto it = m_document.project().transitionTypes.constFind(m_id);
    if (it != m_document.project().transitionTypes.cend()) {
        m_before = *it;
    }
}

void SetTransitionTypeCommand::redo()
{
    m_document.setTransitionType(m_id, m_after);
}

void SetTransitionTypeCommand::undo()
{
    m_document.setTransitionType(m_id, m_before);
}

RenameTransitionTypeCommand::RenameTransitionTypeCommand(ProjectDocument& document, QString from,
                                                         QString to)
    : m_document(document), m_from(std::move(from)), m_to(std::move(to))
{
    Q_ASSERT(isTransitionTypeIdAvailable(m_document.project(), m_to));
}

QString RenameTransitionTypeCommand::text() const
{
    return tr("Rename transition type %1 to %2").arg(m_from, m_to);
}

void RenameTransitionTypeCommand::redo()
{
    m_document.renameTransitionType(m_from, m_to);
}

void RenameTransitionTypeCommand::undo()
{
    m_document.renameTransitionType(m_to, m_from);
}

}  // namespace shoal::core
