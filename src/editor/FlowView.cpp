#include "editor/FlowView.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <algorithm>
#include <cmath>

#include "core/Model.h"
#include "core/ProjectDocument.h"
#include "editor/FlowScene.h"
#include "editor/Icons.h"
#include "editor/SceneNodeItem.h"
#include "editor/TransitionEdgeItem.h"

namespace shoal::editor {

namespace {
constexpr double NEW_SCENE_NUDGE = 24;
}  // namespace

FlowView::FlowView(FlowScene& flowScene, QWidget* parent)
    : CanvasView(&flowScene, parent), m_flowScene(flowScene)
{
}

void FlowView::addScene()
{
    QPointF position = mapToScene(viewport()->rect().center()) -
                       QPointF(SceneNodeItem::WIDTH / 2, NEW_SCENE_NUDGE);
    position = QPointF(std::round(position.x()), std::round(position.y()));
    // Don't hide a node exactly under another one, as repeated adds would.
    const auto& positions = m_flowScene.document().project().flow.nodePositions;
    while (std::any_of(positions.cbegin(), positions.cend(), [&](const core::Point& p) {
        return p.x == position.x() && p.y == position.y();
    })) {
        position += QPointF(NEW_SCENE_NUDGE, NEW_SCENE_NUDGE);
    }
    m_flowScene.addScene(position);
}

void FlowView::renameSelectedScene()
{
    const QStringList selected = m_flowScene.selectedSceneIds();
    if (selected.size() == 1) {
        promptRename(selected.first());
    }
}

void FlowView::setSelectedSceneAsStart()
{
    const QStringList selected = m_flowScene.selectedSceneIds();
    if (selected.size() == 1) {
        m_flowScene.setStartScene(selected.first());
    }
}

void FlowView::removeSelection()
{
    m_flowScene.removeSelection();
}

void FlowView::promptRename(const QString& sceneId)
{
    QString text = sceneId;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getText(this, tr("Rename Scene"),
                                     tr("New id for scene %1 (also its folder name):").arg(sceneId),
                                     QLineEdit::Normal, text, &ok)
                   .trimmed();
        if (!ok || m_flowScene.renameScene(sceneId, text)) {
            return;
        }
        warning(this, tr("Rename Scene"), FlowScene::renameRefusal(text));
    }
}

void FlowView::contextMenuEvent(QContextMenuEvent* event)
{
    const QPointF scenePosition = mapToScene(event->pos());
    QGraphicsItem* item = m_flowScene.itemAt(scenePosition);
    if (item && !item->isSelected()) {
        m_flowScene.clearSelection();
        item->setSelected(true);
    }

    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Add Scene Here"), this, [this, scenePosition] {
            m_flowScene.addScene(scenePosition -
                                 QPointF(SceneNodeItem::WIDTH / 2, NEW_SCENE_NUDGE));
        });
    }
    else if (item->type() == SceneNodeItem::Type) {
        const QString id = static_cast<SceneNodeItem*>(item)->sceneId();
        const bool single = m_flowScene.selectedSceneIds().size() == 1 &&
                            m_flowScene.selectedTransitionIds().isEmpty();
        if (single) {
            menu.addAction(tr("Rename…"), this, [this, id] { promptRename(id); });
            QAction* start = menu.addAction(tr("Set as Start Scene"), this,
                                            [this, id] { m_flowScene.setStartScene(id); });
            start->setEnabled(m_flowScene.document().project().manifest.start != id);
            QMenu* connectMenu = menu.addMenu(tr("Add Transition To"));
            for (const QString& to : m_flowScene.document().project().scenes.keys()) {
                connectMenu->addAction(to, this,
                                       [this, id, to] { m_flowScene.addTransition(id, to); });
            }
            menu.addSeparator();
        }
        menu.addAction(tr("Remove"), this, &FlowView::removeSelection);
    }
    else {
        menu.addAction(tr("Remove"), this, &FlowView::removeSelection);
    }
    menu.exec(event->globalPos());
}

}  // namespace shoal::editor
