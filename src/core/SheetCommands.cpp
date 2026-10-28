#include "core/SheetCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::core::SheetCommands", text);
}

}  // namespace

SetSheetCommand::SetSheetCommand(ProjectDocument& document, QString alias,
                                 std::optional<QString> file)
    : m_document(document), m_alias(std::move(alias)), m_after(std::move(file))
{
    const QMap<QString, QString>& sheets = m_document.project().manifest.sheets;
    if (sheets.contains(m_alias)) {
        m_before = sheets.value(m_alias);
    }
    m_text = (!m_after   ? tr("Remove sheet %1")
              : m_before ? tr("Change sheet %1")
                         : tr("Add sheet %1"))
                 .arg(m_alias);
}

void SetSheetCommand::redo()
{
    m_document.setSheet(m_alias, m_after);
}

void SetSheetCommand::undo()
{
    m_document.setSheet(m_alias, m_before);
}

}  // namespace shoal::core
