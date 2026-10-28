#include "core/FlowCommands.h"

#include <QCoreApplication>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("shoal::core::FlowCommands", text, disambiguation, n);
}

std::optional<Point> nodePosition(const ProjectDocument& document, const QString& id)
{
    const auto& nodes = document.project().flow.nodePositions;
    const auto it = nodes.find(id);
    return it == nodes.end() ? std::nullopt : std::optional<Point>(*it);
}

}  // namespace

// --- AddSceneCommand ---

AddSceneCommand::AddSceneCommand(ProjectDocument& document, QString id, Scene scene,
                                 std::optional<Point> nodePosition)
    : m_document(document),
      m_id(std::move(id)),
      m_scene(std::move(scene)),
      m_nodePosition(nodePosition)
{
    Q_ASSERT(m_document.isSceneIdAvailable(m_id));
}

QString AddSceneCommand::text() const
{
    return tr("Add scene %1").arg(m_id);
}

void AddSceneCommand::redo()
{
    m_document.insertScene(m_id, m_scene);
    if (m_nodePosition) {
        m_document.setSceneNodePosition(m_id, m_nodePosition);
    }
}

void AddSceneCommand::undo()
{
    m_document.setSceneNodePosition(m_id, std::nullopt);
    m_document.removeScene(m_id);
}

// --- RemoveSceneCommand ---

RemoveSceneCommand::RemoveSceneCommand(ProjectDocument& document, QString id)
    : m_document(document), m_id(std::move(id))
{
    const Project& project = m_document.project();
    Q_ASSERT(project.scenes.contains(m_id));
    m_scene = project.scenes.value(m_id);
    m_nodePosition = nodePosition(m_document, m_id);
    for (const QString& transitionId : m_document.transitionsTouching(m_id)) {
        m_transitions.push_back(*m_document.findTransition(transitionId));
    }
    m_wasStart = project.manifest.start == m_id;
}

QString RemoveSceneCommand::text() const
{
    return tr("Remove scene %1").arg(m_id);
}

void RemoveSceneCommand::redo()
{
    for (const Transition& transition : m_transitions) {
        m_document.removeTransition(transition.id);
    }
    if (m_wasStart) {
        m_document.setStartScene({});
    }
    m_document.setSceneNodePosition(m_id, std::nullopt);
    m_document.removeScene(m_id);
}

void RemoveSceneCommand::undo()
{
    m_document.insertScene(m_id, m_scene);
    if (m_nodePosition) {
        m_document.setSceneNodePosition(m_id, m_nodePosition);
    }
    if (m_wasStart) {
        m_document.setStartScene(m_id);
    }
    for (const Transition& transition : m_transitions) {
        m_document.insertTransition(transition);
    }
}

// --- RenameSceneCommand ---

RenameSceneCommand::RenameSceneCommand(ProjectDocument& document, QString from, QString to)
    : m_document(document), m_from(std::move(from)), m_to(std::move(to))
{
    Q_ASSERT(m_document.project().scenes.contains(m_from));
    Q_ASSERT(m_document.isSceneIdAvailable(m_to, m_from));
}

QString RenameSceneCommand::text() const
{
    return tr("Rename scene %1 to %2").arg(m_from, m_to);
}

void RenameSceneCommand::redo()
{
    m_document.renameScene(m_from, m_to);
}

void RenameSceneCommand::undo()
{
    m_document.renameScene(m_to, m_from);
}

// --- MoveSceneNodesCommand ---

MoveSceneNodesCommand::MoveSceneNodesCommand(ProjectDocument& document, QMap<QString, Point> to)
    : m_document(document), m_to(std::move(to))
{
    Q_ASSERT(!m_to.isEmpty());
    for (auto it = m_to.cbegin(); it != m_to.cend(); ++it) {
        m_from.insert(it.key(), nodePosition(m_document, it.key()));
    }
}

QString MoveSceneNodesCommand::text() const
{
    return m_to.size() == 1 ? tr("Move scene %1").arg(m_to.firstKey())
                            : tr("Move %n scenes", nullptr, int(m_to.size()));
}

void MoveSceneNodesCommand::redo()
{
    for (auto it = m_to.cbegin(); it != m_to.cend(); ++it) {
        m_document.setSceneNodePosition(it.key(), *it);
    }
}

void MoveSceneNodesCommand::undo()
{
    for (auto it = m_from.cbegin(); it != m_from.cend(); ++it) {
        m_document.setSceneNodePosition(it.key(), *it);
    }
}

// --- SetStartSceneCommand ---

SetStartSceneCommand::SetStartSceneCommand(ProjectDocument& document, QString id)
    : m_document(document), m_from(document.project().manifest.start), m_to(std::move(id))
{
}

QString SetStartSceneCommand::text() const
{
    return tr("Start at scene %1").arg(m_to);
}

void SetStartSceneCommand::redo()
{
    m_document.setStartScene(m_to);
}

void SetStartSceneCommand::undo()
{
    m_document.setStartScene(m_from);
}

// --- AddTransitionCommand ---

AddTransitionCommand::AddTransitionCommand(ProjectDocument& document, Transition transition)
    : m_document(document), m_transition(std::move(transition))
{
    Q_ASSERT(!m_document.findTransition(m_transition.id));
}

QString AddTransitionCommand::text() const
{
    return tr("Add transition %1").arg(m_transition.id);
}

void AddTransitionCommand::redo()
{
    m_document.insertTransition(m_transition);
}

void AddTransitionCommand::undo()
{
    m_document.removeTransition(m_transition.id);
}

// --- RemoveTransitionCommand ---

RemoveTransitionCommand::RemoveTransitionCommand(ProjectDocument& document, QString id)
    : m_document(document), m_id(std::move(id))
{
    const Transition* transition = m_document.findTransition(m_id);
    Q_ASSERT(transition);
    m_transition = *transition;
}

QString RemoveTransitionCommand::text() const
{
    return tr("Remove transition %1").arg(m_id);
}

void RemoveTransitionCommand::redo()
{
    m_document.removeTransition(m_id);
}

void RemoveTransitionCommand::undo()
{
    m_document.insertTransition(m_transition);
}

}  // namespace shoal::core
