#pragma once

#include <QString>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"

namespace shoal::core {

class ProjectDocument;

/// Declares a class in classes.json, changes its declaration, or removes it (null). The
/// first declaration creates classes.json. Entities keep their classes either way:
/// declarations are documentation, and the classes in use need none.
class SetClassDeclarationCommand : public Command {
public:
    SetClassDeclarationCommand(ProjectDocument& document, QString name,
                               std::optional<ClassDeclaration> declaration);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_text;
    std::optional<ClassRegistry> m_before;
    std::optional<ClassRegistry> m_after;
};

}  // namespace shoal::core
