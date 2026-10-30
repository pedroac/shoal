#pragma once

#include <QString>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"

namespace shoal::core {

class ProjectDocument;

/// Declares an asset of `kind` in the manifest under `alias`, points it at another file,
/// or removes it (null). Entities keep referring to the alias either way.
class SetAssetCommand : public Command {
public:
    SetAssetCommand(ProjectDocument& document, AssetKind kind, QString alias,
                    std::optional<QString> file);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    AssetKind m_kind;
    QString m_alias;
    QString m_text;
    std::optional<QString> m_before;
    std::optional<QString> m_after;
};

}  // namespace shoal::core
