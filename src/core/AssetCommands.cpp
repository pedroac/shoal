#include "core/AssetCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("shoal::core::AssetCommands", text);
}

/// The kind, as a command's text names it.
QString kindName(AssetKind kind)
{
    switch (kind) {
        case AssetKind::Sheet:
            return tr("sheet");
        case AssetKind::Image:
            return tr("image");
        case AssetKind::Font:
            return tr("font");
        case AssetKind::Audio:
            return tr("audio");
        case AssetKind::Video:
            return tr("video");
    }
    return {};
}

}  // namespace

SetAssetCommand::SetAssetCommand(ProjectDocument& document, AssetKind kind, QString alias,
                                 std::optional<QString> file)
    : m_document(document), m_kind(kind), m_alias(std::move(alias)), m_after(std::move(file))
{
    const QMap<QString, QString>& declared = m_document.project().manifest.assets.of(m_kind);
    if (declared.contains(m_alias)) {
        m_before = declared.value(m_alias);
    }
    m_text = (!m_after   ? tr("Remove %1 %2")
              : m_before ? tr("Change %1 %2")
                         : tr("Add %1 %2"))
                 .arg(kindName(m_kind), m_alias);
}

void SetAssetCommand::redo()
{
    m_document.setAsset(m_kind, m_alias, m_after);
}

void SetAssetCommand::undo()
{
    m_document.setAsset(m_kind, m_alias, m_before);
}

}  // namespace shoal::core
