#pragma once

#include <QString>
#include <optional>

#include "core/CommandStack.h"

namespace shoal::core {

class ProjectDocument;

/// Declares a sprat sheet in the manifest under `alias`, points it at another file, or
/// removes it (null). Entities keep referring to the alias either way.
class SetSheetCommand : public Command {
public:
    SetSheetCommand(ProjectDocument& document, QString alias, std::optional<QString> file);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_alias;
    QString m_text;
    std::optional<QString> m_before;
    std::optional<QString> m_after;
};

}  // namespace shoal::core
