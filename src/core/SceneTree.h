#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

#include "core/Model.h"

namespace shoal::core {

/// Where an entity is in its scene: the index among the top-level entities, then among
/// each parent's children. Entities need no id, so this is how the editor names them.
using EntityPath = QList<int>;

/// The entity at `path`, or null.
const Entity* entityAt(const Scene& scene, const EntityPath& path);
Entity* entityAt(Scene& scene, const EntityPath& path);

/// The list `path` indexes into: the scene's entities for a top-level path, otherwise the
/// parent's children. Null if the parent doesn't exist.
std::vector<Entity>* siblingsOf(Scene& scene, const EntityPath& path);

/// The layer an entity is on: its own, or its top-level ancestor's.
QString layerOf(const Scene& scene, const EntityPath& path);

/// Whether `ancestor` is a strict ancestor of `path`.
bool isAncestor(const EntityPath& ancestor, const EntityPath& path);

/// Visits every entity, parents before children, in draw order.
void forEachEntity(const Scene& scene,
                   const std::function<void(const Entity&, const EntityPath&)>& visit);

/// The path of the entity with `id`, or empty.
EntityPath findEntity(const Scene& scene, QStringView id);

/// The ids in use in the scene.
QStringList entityIds(const Scene& scene);

/// The index of the layer with `id`, or -1.
int layerIndex(const Scene& scene, QStringView id);

/// `path` as it reads once the entity at `removed` is taken out of the tree. `path` must
/// not be inside `removed`.
EntityPath pathAfterRemoval(EntityPath path, const EntityPath& removed);

/// `paths` without those inside another one of them, sorted.
QList<EntityPath> outermost(QList<EntityPath> paths);

}  // namespace shoal::core
