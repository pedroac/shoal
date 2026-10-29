#include "core/SceneCommands.h"

#include <QCoreApplication>
#include <algorithm>

#include "core/ProjectDocument.h"

namespace shoal::core {

namespace {

QString tr(const char* text, int n = -1)
{
    return QCoreApplication::translate("shoal::core::SceneCommands", text, nullptr, n);
}

const Scene& sceneOf(const ProjectDocument& document, const QString& sceneId)
{
    const auto it = document.project().scenes.find(sceneId);
    Q_ASSERT(it != document.project().scenes.end());
    return *it;
}

Entity withoutChildren(Entity entity)
{
    entity.children.clear();
    return entity;
}

}  // namespace

// --- AddEntityCommand ---

AddEntityCommand::AddEntityCommand(ProjectDocument& document, QString sceneId, EntityPath path,
                                   Entity entity)
    : m_document(document),
      m_sceneId(std::move(sceneId)),
      m_path(std::move(path)),
      m_entity(std::move(entity))
{
}

QString AddEntityCommand::text() const
{
    return tr("Add %1").arg(describe(m_entity));
}

void AddEntityCommand::redo()
{
    m_document.insertEntity(m_sceneId, m_path, m_entity);
}

void AddEntityCommand::undo()
{
    m_document.removeEntity(m_sceneId, m_path);
}

// --- RemoveEntitiesCommand ---

RemoveEntitiesCommand::RemoveEntitiesCommand(ProjectDocument& document, QString sceneId,
                                             QList<EntityPath> paths)
    : m_document(document), m_sceneId(std::move(sceneId))
{
    const Scene& scene = sceneOf(m_document, m_sceneId);
    for (const EntityPath& path : outermost(std::move(paths))) {
        const Entity* entity = entityAt(scene, path);
        Q_ASSERT(entity);
        m_removed.emplace_back(path, *entity);
    }
}

QString RemoveEntitiesCommand::text() const
{
    return m_removed.size() == 1 ? tr("Remove %1").arg(describe(m_removed.front().second))
                                 : tr("Remove %n entities", int(m_removed.size()));
}

void RemoveEntitiesCommand::redo()
{
    // Last first, so the paths before it stay valid.
    for (auto it = m_removed.rbegin(); it != m_removed.rend(); ++it) {
        m_document.removeEntity(m_sceneId, it->first);
    }
}

void RemoveEntitiesCommand::undo()
{
    for (const auto& [path, entity] : m_removed) {
        m_document.insertEntity(m_sceneId, path, entity);
    }
}

// --- EditEntitiesCommand ---

EditEntitiesCommand::EditEntitiesCommand(ProjectDocument& document, QString sceneId,
                                         QList<std::pair<EntityPath, Entity>> changes, QString text)
    : m_document(document), m_sceneId(std::move(sceneId)), m_text(std::move(text))
{
    const Scene& scene = sceneOf(m_document, m_sceneId);
    QMap<QString, QString> renamed;
    for (auto& [path, entity] : changes) {
        const Entity* current = entityAt(scene, path);
        Q_ASSERT(current);
        if (!current->id.isEmpty() && current->id != entity.id) {
            renamed.insert(current->id, entity.id);
        }
        m_before.emplace_back(path, withoutChildren(*current));
        m_after.emplace_back(path, withoutChildren(std::move(entity)));
    }

    // Transitions name this scene's entities in `persist.entities`: carried ones as keys
    // when they leave from here, marks as values when they arrive here.
    if (renamed.isEmpty()) {
        return;
    }
    for (const Transition& transition : m_document.project().flow.transitions) {
        Transition updated = transition;
        QMap<QString, QString> entities;
        for (auto it = transition.persist.entities.cbegin();
             it != transition.persist.entities.cend(); ++it) {
            QString carried = it.key();
            QString mark = it.value();
            if (transition.from == m_sceneId && renamed.contains(carried)) {
                carried = renamed.value(carried);
            }
            if (transition.to == m_sceneId && renamed.contains(mark)) {
                mark = renamed.value(mark);
            }
            entities.insert(carried, mark);
        }
        updated.persist.entities = entities;
        if (updated != transition) {
            m_transitionsBefore.push_back(transition);
            m_transitionsAfter.push_back(std::move(updated));
        }
    }
}

void EditEntitiesCommand::redo()
{
    for (const auto& [path, entity] : m_after) {
        m_document.setEntityFields(m_sceneId, path, entity);
    }
    for (const Transition& transition : m_transitionsAfter) {
        m_document.setTransition(transition);
    }
}

void EditEntitiesCommand::undo()
{
    for (const Transition& transition : m_transitionsBefore) {
        m_document.setTransition(transition);
    }
    for (const auto& [path, entity] : m_before) {
        m_document.setEntityFields(m_sceneId, path, entity);
    }
}

// --- MoveEntityCommand ---

MoveEntityCommand::MoveEntityCommand(ProjectDocument& document, QString sceneId, EntityPath from,
                                     EntityPath to, const ImageMetricsFn& metrics)
    : m_document(document),
      m_sceneId(std::move(sceneId)),
      m_from(std::move(from)),
      m_to(std::move(to))
{
    const Scene& scene = sceneOf(m_document, m_sceneId);
    Q_ASSERT(entityAt(scene, m_from) && !m_to.isEmpty());
    m_name = describe(*entityAt(scene, m_from));
    m_fromFields = *entityAt(scene, m_from);
    m_fromFields.children.clear();
    m_toFields = relocatedEntity(scene, m_from, m_to, metrics);
}

QString MoveEntityCommand::text() const
{
    return tr("Move %1").arg(m_name);
}

void MoveEntityCommand::redo()
{
    Entity entity = m_document.removeEntity(m_sceneId, m_from);
    entity.transform = m_toFields.transform;
    entity.at = m_toFields.at;
    entity.size = m_toFields.size;
    m_document.insertEntity(m_sceneId, m_to, std::move(entity));
}

void MoveEntityCommand::undo()
{
    Entity entity = m_document.removeEntity(m_sceneId, m_to);
    entity.transform = m_fromFields.transform;
    entity.at = m_fromFields.at;
    entity.size = m_fromFields.size;
    m_document.insertEntity(m_sceneId, m_from, std::move(entity));
}

// --- ReorderEntitiesCommand ---

ReorderEntitiesCommand::ReorderEntitiesCommand(ProjectDocument& document, QString sceneId,
                                               EntityPath parent, QList<int> order, QString text)
    : m_document(document),
      m_sceneId(std::move(sceneId)),
      m_parent(std::move(parent)),
      m_order(std::move(order)),
      m_text(std::move(text))
{
}

void ReorderEntitiesCommand::redo()
{
    m_document.reorderEntities(m_sceneId, m_parent, m_order);
}

void ReorderEntitiesCommand::undo()
{
    QList<int> inverse(m_order.size());
    for (qsizetype i = 0; i < m_order.size(); ++i) {
        inverse[m_order[i]] = int(i);
    }
    m_document.reorderEntities(m_sceneId, m_parent, inverse);
}

}  // namespace shoal::core
