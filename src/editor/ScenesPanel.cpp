#include "editor/ScenesPanel.h"

#include <QListWidget>
#include <QVBoxLayout>

#include "core/ProjectDocument.h"
#include "editor/FlowScene.h"

namespace shoal::editor {

ScenesPanel::ScenesPanel(QWidget* parent) : QWidget(parent), m_list(new QListWidget(this))
{
    m_list->setObjectName(QStringLiteral("scenesList"));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins({});
    layout->addWidget(m_list);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ScenesPanel::onListSelectionChanged);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* row) {
        emit sceneActivated(row->data(Qt::UserRole).toString());
    });
    setFlowScene(nullptr);
}

void ScenesPanel::setFlowScene(FlowScene* flow)
{
    if (m_flow) {
        disconnect(m_flow, nullptr, this, nullptr);
        disconnect(&m_flow->document(), nullptr, this, nullptr);
    }
    m_flow = flow;
    if (m_flow) {
        core::ProjectDocument* document = &m_flow->document();
        connect(document, &core::ProjectDocument::sceneAdded, this, &ScenesPanel::rebuild);
        connect(document, &core::ProjectDocument::sceneRemoved, this, &ScenesPanel::rebuild);
        connect(document, &core::ProjectDocument::sceneRenamed, this, &ScenesPanel::rebuild);
        connect(document, &core::ProjectDocument::startSceneChanged, this, &ScenesPanel::rebuild);
        connect(m_flow, &QGraphicsScene::selectionChanged, this, &ScenesPanel::showSelection);
    }
    rebuild();
}

QListWidgetItem* ScenesPanel::sceneRow(const QString& sceneId) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == sceneId) {
            return m_list->item(i);
        }
    }
    return nullptr;
}

void ScenesPanel::rebuild()
{
    m_syncing = true;
    m_list->clear();
    if (m_flow) {
        const core::Project& project = m_flow->document().project();
        for (const QString& id : project.scenes.keys()) {
            auto* row = new QListWidgetItem(m_list);
            row->setData(Qt::UserRole, id);
            if (id == project.manifest.start) {
                row->setText(tr("%1 (start)").arg(id));
                QFont font = row->font();
                font.setBold(true);
                row->setFont(font);
                row->setToolTip(tr("The scene the game starts at"));
            }
            else {
                row->setText(id);
            }
        }
    }
    m_syncing = false;
    showSelection();
}

void ScenesPanel::showSelection()
{
    if (m_syncing || !m_flow) {
        return;
    }
    m_syncing = true;
    const QStringList selected = m_flow->selectedSceneIds();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* row = m_list->item(i);
        row->setSelected(selected.contains(row->data(Qt::UserRole).toString()));
    }
    if (selected.size() == 1) {
        m_list->scrollToItem(sceneRow(selected.first()));
    }
    m_syncing = false;
}

void ScenesPanel::onListSelectionChanged()
{
    if (m_syncing || !m_flow) {
        return;
    }
    QStringList ids;
    for (const QListWidgetItem* row : m_list->selectedItems()) {
        ids << row->data(Qt::UserRole).toString();
    }
    m_syncing = true;
    m_flow->select(ids);
    m_syncing = false;
}

}  // namespace shoal::editor
