#include "editor/FlowScene.h"

#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "core/FlowCommands.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "editor/SceneNodeItem.h"
#include "editor/TransitionEdgeItem.h"

namespace shoal::editor {

using core::Point;
using core::Transition;
using core::TransitionMode;

namespace {
constexpr double EDGE_SPREAD = 40;     ///< Between edges that join the same two nodes.
constexpr double PLACEMENT_GAP = 60;   ///< Around nodes placed by the view.
constexpr double PLACEMENT_ROW = 120;  ///< The height of a row of placed nodes.
const QString NEW_SCENE_ID = QStringLiteral("scene");

QPointF toQPointF(Point point)
{
    return {point.x, point.y};
}

}  // namespace

FlowScene::FlowScene(core::ProjectDocument& document, QObject* parent)
    : QGraphicsScene(parent), m_document(document)
{
    connect(&m_document, &core::ProjectDocument::sceneRenamed, this, &FlowScene::onSceneRenamed);
    connect(&m_document, &core::ProjectDocument::changed, this, &FlowScene::sync);
    sync();
}

FlowScene::~FlowScene()
{
    // Delete the items while this is still a FlowScene: deleting selected items emits
    // selectionChanged(), and its receivers query this scene.
    clear();
    m_nodes.clear();
    m_edges.clear();
}

SceneNodeItem* FlowScene::node(const QString& sceneId) const
{
    return m_nodes.value(sceneId);
}

TransitionEdgeItem* FlowScene::edge(const QString& transitionId) const
{
    return m_edges.value(transitionId);
}

QGraphicsItem* FlowScene::itemAt(QPointF position) const
{
    QGraphicsItem* found = nullptr;
    for (QGraphicsItem* item : items(position)) {  // topmost first
        if (item->type() == SceneNodeItem::Type) {
            return item;
        }
        if (item->type() == TransitionEdgeItem::Type && !found) {
            found = item;
        }
    }
    return found;
}

QStringList FlowScene::selectedSceneIds() const
{
    QStringList ids;
    for (QGraphicsItem* item : selectedItems()) {
        if (item->type() == SceneNodeItem::Type) {
            ids << static_cast<SceneNodeItem*>(item)->sceneId();
        }
    }
    ids.sort();
    return ids;
}

QStringList FlowScene::selectedTransitionIds() const
{
    QStringList ids;
    for (QGraphicsItem* item : selectedItems()) {
        if (item->type() == TransitionEdgeItem::Type) {
            ids << static_cast<TransitionEdgeItem*>(item)->transitionId();
        }
    }
    ids.sort();
    return ids;
}

void FlowScene::select(const QStringList& sceneIds, const QStringList& transitionIds)
{
    QSignalBlocker blocker(this);  // one selectionChanged() for the whole change
    clearSelection();
    for (const QString& id : sceneIds) {
        if (SceneNodeItem* item = node(id)) {
            item->setSelected(true);
        }
    }
    for (const QString& id : transitionIds) {
        if (TransitionEdgeItem* item = edge(id)) {
            item->setSelected(true);
        }
    }
    blocker.unblock();
    emit selectionChanged();
}

// --- edits ---

QString FlowScene::addScene(QPointF position)
{
    const QString id = m_document.uniqueSceneId(NEW_SCENE_ID);
    const Point at{std::round(position.x()), std::round(position.y())};
    m_document.commands().push(std::make_unique<core::AddSceneCommand>(
        m_document, id, core::newScene(m_document.project().manifest), at));
    select({id});
    return id;
}

void FlowScene::removeSelection()
{
    const QStringList scenes = selectedSceneIds();
    const QStringList transitions = selectedTransitionIds();
    const auto count = scenes.size() + transitions.size();
    if (count == 0) {
        return;
    }
    QString text;
    if (count > 1) {
        text = tr("Remove %n items", nullptr, int(count));
    }
    else if (scenes.isEmpty()) {
        text = tr("Remove transition %1").arg(transitions.first());
    }
    else {
        text = tr("Remove scene %1").arg(scenes.first());
    }
    core::CommandStack& commands = m_document.commands();
    commands.beginMacro(text);
    // Transitions first: a scene's removal takes the transitions still touching it.
    for (const QString& id : transitions) {
        if (m_document.findTransition(id)) {
            commands.push(std::make_unique<core::RemoveTransitionCommand>(m_document, id));
        }
    }
    for (const QString& id : scenes) {
        commands.push(std::make_unique<core::RemoveSceneCommand>(m_document, id));
    }
    commands.endMacro();
}

bool FlowScene::renameScene(const QString& from, const QString& to)
{
    if (from == to) {
        return true;
    }
    if (!m_document.project().scenes.contains(from) || !m_document.isSceneIdAvailable(to, from)) {
        return false;
    }
    m_document.commands().push(std::make_unique<core::RenameSceneCommand>(m_document, from, to));
    return true;
}

void FlowScene::setStartScene(const QString& sceneId)
{
    if (m_document.project().manifest.start != sceneId) {
        m_document.commands().push(
            std::make_unique<core::SetStartSceneCommand>(m_document, sceneId));
    }
}

QString FlowScene::addTransition(const QString& from, const QString& to)
{
    Transition transition;
    transition.id = m_document.uniqueTransitionId(QStringLiteral("%1_to_%2").arg(from, to));
    transition.from = from;
    transition.to = to;
    transition.mode = TransitionMode::Replace;
    const QString id = transition.id;
    m_document.commands().push(
        std::make_unique<core::AddTransitionCommand>(m_document, std::move(transition)));
    select({}, {id});
    return id;
}

// --- for the items ---

void FlowScene::commitNodeMoves()
{
    QMap<QString, Point> moves;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        const QPointF position = it.value()->pos();
        if (position != storedPosition(it.key())) {
            moves.insert(it.key(), Point{std::round(position.x()), std::round(position.y())});
        }
    }
    if (!moves.isEmpty()) {
        m_document.commands().push(
            std::make_unique<core::MoveSceneNodesCommand>(m_document, std::move(moves)));
    }
}

void FlowScene::handleNodeMoved(const SceneNodeItem& node)
{
    for (TransitionEdgeItem* edge : std::as_const(m_edges)) {
        if (edge->from() == node.sceneId() || edge->to() == node.sceneId()) {
            edge->updateGeometry();
        }
    }
}

void FlowScene::beginConnection(const SceneNodeItem& from, QPointF position)
{
    m_connectionFrom = from.sceneId();
    m_connection = addPath({}, QPen(palette().color(QPalette::Highlight), 1.5, Qt::DashLine));
    m_connection->setZValue(2);
    updateConnection(position);
}

void FlowScene::updateConnection(QPointF position)
{
    const SceneNodeItem* from = node(m_connectionFrom);
    if (!m_connection || !from) {
        return;
    }
    QPainterPath path(from->mapToScene(from->portRect().center()));
    path.lineTo(position);
    m_connection->setPath(path);
}

void FlowScene::finishConnection(QPointF position)
{
    delete m_connection;
    m_connection = nullptr;
    const QString from = std::exchange(m_connectionFrom, {});

    QGraphicsItem* target = itemAt(position);
    if (target && target->type() == SceneNodeItem::Type) {
        const QString to = static_cast<SceneNodeItem*>(target)->sceneId();
        // Dropping back on the start node is most likely a slip; a transition to the same
        // scene is still possible from the node's context menu.
        if (to != from && node(from)) {
            addTransition(from, to);
        }
    }
}

// --- sync ---

void FlowScene::onSceneRenamed(const QString& from, const QString& to)
{
    if (SceneNodeItem* item = m_nodes.take(from)) {
        item->setSceneId(to);
        m_nodes.insert(to, item);
    }
    if (m_placed.contains(from)) {
        m_placed.insert(to, m_placed.take(from));
    }
}

void FlowScene::sync()
{
    syncNodes();
    syncEdges();
}

QPointF FlowScene::storedPosition(const QString& sceneId) const
{
    const auto& positions = m_document.project().flow.nodePositions;
    const auto it = positions.find(sceneId);
    return it != positions.end() ? toQPointF(*it) : m_placed.value(sceneId);
}

void FlowScene::syncNodes()
{
    const core::Project& project = m_document.project();
    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        if (!project.scenes.contains(it.key())) {
            m_placed.remove(it.key());
            delete it.value();
            it = m_nodes.erase(it);
        }
        else {
            ++it;
        }
    }

    // Transitions that lead to no node are listed on the node they leave from.
    QHash<QString, QStringList> exits;
    for (const Transition& transition : project.flow.transitions) {
        if (transition.mode == TransitionMode::Pop) {
            exits[transition.from] << tr("%1 → back").arg(transition.id);
        }
        else if (transition.mode == TransitionMode::PushModifier) {
            exits[transition.from]
                << tr("%1 → modifier %2").arg(transition.id, transition.modifier);
        }
        else if (!project.scenes.contains(transition.to)) {
            exits[transition.from] << tr("%1 → %2 (missing)").arg(transition.id, transition.to);
        }
    }

    for (auto it = project.scenes.cbegin(); it != project.scenes.cend(); ++it) {
        SceneNodeItem* item = m_nodes.value(it.key());
        if (!item) {
            item = new SceneNodeItem(*this, it.key());
            addItem(item);
            m_nodes.insert(it.key(), item);
        }
        item->setStart(project.manifest.start == it.key());
        item->setModifierCount(int(it->modifiers.size()));
        item->setExits(exits.value(it.key()));
    }

    placeUnpositionedNodes();
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        const QPointF position = storedPosition(it.key());
        if (it.value()->pos() != position) {
            it.value()->setPos(position);
        }
    }
}

void FlowScene::placeUnpositionedNodes()
{
    const auto& positions = m_document.project().flow.nodePositions;
    QStringList unplaced;
    for (const QString& id : m_document.project().scenes.keys()) {  // in id order
        if (!positions.contains(id) && !m_placed.contains(id)) {
            unplaced << id;
        }
    }
    if (unplaced.isEmpty()) {
        return;
    }

    // In rows below everything shown so far.
    double left = 0;
    double bottom = -PLACEMENT_GAP;
    bool first = true;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        if (unplaced.contains(it.key())) {
            continue;
        }
        const QPointF position = storedPosition(it.key());
        const double height = it.value()->bodyRect().height();
        left = first ? position.x() : std::min(left, position.x());
        bottom = first ? position.y() + height : std::max(bottom, position.y() + height);
        first = false;
    }
    constexpr int PER_ROW = 4;
    for (int i = 0; i < int(unplaced.size()); ++i) {
        m_placed.insert(unplaced[i],
                        QPointF(left + double(i % PER_ROW) * (SceneNodeItem::WIDTH + PLACEMENT_GAP),
                                bottom + PLACEMENT_GAP + double(i / PER_ROW) * PLACEMENT_ROW));
    }
}

void FlowScene::syncEdges()
{
    const core::Project& project = m_document.project();

    // Edges between the same two nodes, in either direction, are spread apart.
    std::map<std::pair<QString, QString>, QList<const Transition*>> groups;
    for (const Transition& transition : project.flow.transitions) {
        const bool drawable = transition.mode != TransitionMode::Pop &&
                              transition.mode != TransitionMode::PushModifier &&
                              m_nodes.contains(transition.from) && m_nodes.contains(transition.to);
        if (drawable) {
            groups[std::minmax(transition.from, transition.to)] << &transition;
        }
    }

    QHash<QString, TransitionEdgeItem*> kept;
    for (const auto& [pair, transitions] : groups) {
        const auto count = transitions.size();
        for (qsizetype i = 0; i < count; ++i) {
            const Transition& transition = *transitions[i];
            TransitionEdgeItem* item = m_edges.take(transition.id);
            if (!item) {
                item = new TransitionEdgeItem(*this, transition.id);
                addItem(item);
            }
            double bend = 0;
            if (pair.first == pair.second) {
                bend = double(i);  // loops nest
            }
            else {
                bend = (double(i) - double(count - 1) / 2) * EDGE_SPREAD;
                if (transition.from != pair.first) {
                    bend = -bend;  // its left is the other way round
                }
            }
            item->setTransition(transition, bend);
            kept.insert(transition.id, item);
        }
    }
    qDeleteAll(m_edges);  // what is left no longer has an edge
    m_edges = std::move(kept);
}

}  // namespace shoal::editor
