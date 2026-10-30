#pragma once

#include <QString>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"

namespace shoal::core {

class ProjectDocument;

/// Adds, replaces or removes (null) a project preset. On a built-in preset's id, adding
/// one customizes it, and removing it brings back the built-in one.
class SetPresetCommand : public Command {
public:
    SetPresetCommand(ProjectDocument& document, QString id, std::optional<Preset> preset,
                     QString text);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    std::optional<Preset> m_before;
    std::optional<Preset> m_after;
    QString m_text;
};

/// Gives a project preset another id, its file's name. `to` must be a valid id no preset,
/// built-in or not, has.
class RenamePresetCommand : public Command {
public:
    RenamePresetCommand(ProjectDocument& document, QString from, QString to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_from;
    QString m_to;
};

}  // namespace shoal::core
