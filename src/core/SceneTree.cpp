#include "core/SceneTree.h"

#include <QSet>
#include <algorithm>
#include <numeric>
#include <utility>

namespace shoal::core {

namespace {

template <typename SceneT, typename EntityT>
EntityT* find(SceneT& scene, const EntityPath& path)
{
    auto* list = &scene.entities;
    EntityT* entity = nullptr;
    for (int index : path) {
        if (index < 0 || size_t(index) >= list->size()) {
            return nullptr;
        }
        entity = &(*list)[size_t(index)];
        list = &entity->children;
    }
    return entity;
}

void visitAll(const std::vector<Entity>& entities, EntityPath& path,
              const std::function<void(const Entity&, const EntityPath&)>& visit)
{
    for (size_t i = 0; i < entities.size(); ++i) {
        path.append(int(i));
        visit(entities[i], path);
        visitAll(entities[i].children, path, visit);
        path.removeLast();
    }
}

}  // namespace

const Entity* entityAt(const Scene& scene, const EntityPath& path)
{
    return find<const Scene, const Entity>(scene, path);
}

Entity* entityAt(Scene& scene, const EntityPath& path)
{
    return find<Scene, Entity>(scene, path);
}

std::vector<Entity>* siblingsOf(Scene& scene, const EntityPath& path)
{
    if (path.size() <= 1) {
        return &scene.entities;
    }
    Entity* parent = entityAt(scene, path.first(path.size() - 1));
    return parent ? &parent->children : nullptr;
}

QString layerOf(const Scene& scene, const EntityPath& path)
{
    const Entity* top = path.isEmpty() ? nullptr : entityAt(scene, {path.first()});
    return top ? top->layer : QString();
}

bool isAncestor(const EntityPath& ancestor, const EntityPath& path)
{
    return ancestor.size() < path.size() && path.first(ancestor.size()) == ancestor;
}

QString describe(const Entity& entity)
{
    return entity.id.isEmpty() ? enumName(entity.type) : entity.id;
}

void forEachEntity(const Scene& scene,
                   const std::function<void(const Entity&, const EntityPath&)>& visit)
{
    EntityPath path;
    visitAll(scene.entities, path, visit);
}

EntityPath findEntity(const Scene& scene, QStringView id)
{
    EntityPath found;
    if (id.isEmpty()) {
        return found;
    }
    forEachEntity(scene, [&](const Entity& entity, const EntityPath& path) {
        if (found.isEmpty() && entity.id == id) {
            found = path;
        }
    });
    return found;
}

QStringList entityIds(const Scene& scene)
{
    QStringList ids;
    forEachEntity(scene, [&](const Entity& entity, const EntityPath&) {
        if (!entity.id.isEmpty()) {
            ids << entity.id;
        }
    });
    return ids;
}

int layerIndex(const Scene& scene, QStringView id)
{
    for (size_t i = 0; i < scene.layers.size(); ++i) {
        if (scene.layers[i].id == id) {
            return int(i);
        }
    }
    return -1;
}

EntityPath pathAfterRemoval(EntityPath path, const EntityPath& removed)
{
    Q_ASSERT(!removed.isEmpty() && path != removed && !isAncestor(removed, path));
    const qsizetype depth = removed.size() - 1;
    if (path.size() > depth && path.first(depth) == removed.first(depth) &&
        path[depth] > removed[depth]) {
        --path[depth];
    }
    return path;
}

QList<EntityPath> outermost(QList<EntityPath> paths)
{
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    QList<EntityPath> result;
    for (const EntityPath& path : paths) {
        if (result.isEmpty() || !isAncestor(result.back(), path)) {
            result << path;
        }
    }
    return result;
}

namespace {

/// Reorders `members`, sibling indices in draw order, as arranging the `arranged` ones does.
void arrangeMembers(QList<int>& members, const QSet<int>& arranged, Arrange arrange)
{
    const auto isArranged = [&](int index) { return arranged.contains(index); };
    switch (arrange) {
        case Arrange::ToFront:
            std::stable_partition(members.begin(), members.end(),
                                  [&](int index) { return !isArranged(index); });
            break;
        case Arrange::ToBack:
            std::stable_partition(members.begin(), members.end(), isArranged);
            break;
        case Arrange::Forward:
            // From the front, so a run of arranged entities moves as a block.
            for (qsizetype i = members.size() - 2; i >= 0; --i) {
                if (isArranged(members[i]) && !isArranged(members[i + 1])) {
                    std::swap(members[i], members[i + 1]);
                }
            }
            break;
        case Arrange::Backward:
            for (qsizetype i = 1; i < members.size(); ++i) {
                if (isArranged(members[i]) && !isArranged(members[i - 1])) {
                    std::swap(members[i], members[i - 1]);
                }
            }
            break;
    }
}

}  // namespace

QMap<EntityPath, QList<int>> arrangedOrders(const Scene& scene, const QList<EntityPath>& paths,
                                            Arrange arrange)
{
    QMap<EntityPath, QSet<int>> arranged;  // by parent
    for (const EntityPath& path : paths) {
        if (entityAt(scene, path)) {
            arranged[path.first(path.size() - 1)].insert(path.back());
        }
    }
    QMap<EntityPath, QList<int>> orders;
    for (auto it = arranged.cbegin(); it != arranged.cend(); ++it) {
        const EntityPath& parent = it.key();
        const std::vector<Entity>& siblings =
            parent.isEmpty() ? scene.entities : entityAt(scene, parent)->children;
        // The indices that trade places, by layer at the top level.
        QMap<QString, QList<int>> groups;
        for (int i = 0; i < int(siblings.size()); ++i) {
            groups[parent.isEmpty() ? siblings[size_t(i)].layer : QString()] << i;
        }
        QList<int> order(qsizetype(siblings.size()));
        std::iota(order.begin(), order.end(), 0);
        const QList<int> before = order;
        for (const QList<int>& places : std::as_const(groups)) {
            QList<int> members = places;
            arrangeMembers(members, it.value(), arrange);
            for (qsizetype k = 0; k < places.size(); ++k) {
                order[places[k]] = members[k];
            }
        }
        if (order != before) {
            orders.insert(parent, order);
        }
    }
    return orders;
}

EntityPath pathAfterReorder(const EntityPath& path, const QMap<EntityPath, QList<int>>& orders)
{
    EntityPath result = path;
    for (qsizetype depth = 0; depth < path.size(); ++depth) {
        const auto it = orders.constFind(path.first(depth));  // keyed by paths before
        if (it != orders.cend()) {
            result[depth] = int(it->indexOf(path[depth]));
        }
    }
    return result;
}

}  // namespace shoal::core
