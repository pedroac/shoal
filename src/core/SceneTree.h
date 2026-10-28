#pragma once

#include <QList>
#include <QMap>
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

/// An entity's name in a command's text: its id, or its type.
QString describe(const Entity& entity);

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

/// Where arranged entities go in draw order, among their siblings.
enum class Arrange { ToFront, Forward, Backward, ToBack };

/// The new order of each list of siblings that arranging the entities at `paths` changes,
/// by parent path (empty for the top level): `order[i]` is the index, before, of the entity
/// that ends at index i. Forward and backward pass the next sibling that isn't arranged too.
/// Top-level entities trade places only with those on their own layer.
QMap<EntityPath, QList<int>> arrangedOrders(const Scene& scene, const QList<EntityPath>& paths,
                                            Arrange arrange);

/// `path` once the sibling lists are reordered as `orders` says (see arrangedOrders()).
EntityPath pathAfterReorder(const EntityPath& path, const QMap<EntityPath, QList<int>>& orders);

}  // namespace shoal::core
