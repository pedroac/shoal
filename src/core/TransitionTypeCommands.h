#pragma once

#include <QString>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"

namespace shoal::core {

class ProjectDocument;

/// Adds, replaces or removes (null) a project transition type. On a built-in type's id,
/// adding one replaces it, and removing it brings back the built-in one.
class SetTransitionTypeCommand : public Command {
public:
    SetTransitionTypeCommand(ProjectDocument& document, QString id,
                             std::optional<TransitionType> type, QString text);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    std::optional<TransitionType> m_before;
    std::optional<TransitionType> m_after;
    QString m_text;
};

/// Gives a project transition type another id, its file's name, and the effects of that
/// type with it. `to` must be a valid id no type, built-in or not, has.
class RenameTransitionTypeCommand : public Command {
public:
    RenameTransitionTypeCommand(ProjectDocument& document, QString from, QString to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_from;
    QString m_to;
};

}  // namespace shoal::core
