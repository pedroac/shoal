#include "core/ClassCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::core::ClassCommands", text);
}

}  // namespace

SetClassDeclarationCommand::SetClassDeclarationCommand(ProjectDocument& document, QString name,
                                                       std::optional<ClassDeclaration> declaration)
    : m_document(document), m_before(document.project().classes)
{
    const bool declared = m_before && m_before->classes.contains(name);
    m_after = m_before.value_or(ClassRegistry{});
    if (declaration) {
        m_after->classes.insert(name, std::move(*declaration));
        m_text = (declared ? tr("Edit class %1") : tr("Declare class %1")).arg(name);
    }
    else {
        m_after->classes.remove(name);
        m_text = tr("Undeclare class %1").arg(name);
    }
}

void SetClassDeclarationCommand::redo()
{
    m_document.setClassRegistry(m_after);
}

void SetClassDeclarationCommand::undo()
{
    m_document.setClassRegistry(m_before);
}

}  // namespace shoal::core
