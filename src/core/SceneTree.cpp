#include "core/SceneTree.h"

#include <algorithm>

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

}  // namespace shoal::core
