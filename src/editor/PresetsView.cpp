#include "editor/PresetsView.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "core/Presets.h"
#include "core/ProjectDocument.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneView.h"

namespace shoal::editor {

PresetsView::PresetsView(core::ProjectDocument& document, assets::SheetCache& sheets,
                         QWidget* parent)
    : QWidget(parent),
      m_document(document),
      m_sheets(sheets),
      m_placeholder(new QLabel(tr("Select a preset in the Presets list to edit it."), this))
{
    m_placeholder->setAlignment(Qt::AlignCenter);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_placeholder);
}

void PresetsView::setSnapping(bool snapping)
{
    m_snapping = snapping;
    if (m_view) {
        m_view->canvas().setSnapping(snapping);
    }
}

void PresetsView::showPreset(const QString& presetId)
{
    const QString tree = presetId.isEmpty() ? QString() : core::presetTreeId(presetId);
    if (m_view && m_view->canvas().sceneId() == tree) {
        return;  // the same preset, renamed
    }
    if (m_view) {
        SceneView* old = m_view;
        m_view = nullptr;
        emit viewChanged();  // before it goes, so nothing points at its canvas
        old->deleteLater();
    }
    m_placeholder->setVisible(presetId.isEmpty());
    if (!presetId.isEmpty()) {
        m_view = new SceneView(m_document, m_sheets, tree, this);
        m_view->setObjectName(QStringLiteral("presetView"));
        m_view->canvas().setSnapping(m_snapping);
        layout()->addWidget(m_view);
        QTimer::singleShot(0, m_view, &SceneView::fitAll);
        emit viewChanged();
    }
}

}  // namespace shoal::editor
