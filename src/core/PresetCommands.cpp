#include "core/PresetCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::core::PresetCommands", text);
}

}  // namespace

SetPresetCommand::SetPresetCommand(ProjectDocument& document, QString id,
                                   std::optional<Preset> preset, QString text)
    : m_document(document),
      m_id(std::move(id)),
      m_before(document.project().presets.contains(m_id)
                   ? std::optional(document.project().presets.value(m_id))
                   : std::nullopt),
      m_after(std::move(preset)),
      m_text(std::move(text))
{
}

void SetPresetCommand::redo()
{
    m_document.setPreset(m_id, m_after);
}

void SetPresetCommand::undo()
{
    m_document.setPreset(m_id, m_before);
}

RenamePresetCommand::RenamePresetCommand(ProjectDocument& document, QString from, QString to)
    : m_document(document), m_from(std::move(from)), m_to(std::move(to))
{
    Q_ASSERT(document.project().presets.contains(m_from));
}

QString RenamePresetCommand::text() const
{
    return tr("Rename preset %1 to %2").arg(m_from, m_to);
}

void RenamePresetCommand::redo()
{
    m_document.renamePreset(m_from, m_to);
}

void RenamePresetCommand::undo()
{
    m_document.renamePreset(m_to, m_from);
}

}  // namespace shoal::core
