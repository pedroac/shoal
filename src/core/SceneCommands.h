#pragma once

#include <QList>
#include <QString>
#include <utility>
#include <vector>

#include "core/CommandStack.h"
#include "core/Model.h"
#include "core/SceneTree.h"

namespace shoal::core {

class ProjectDocument;

/// Adds an entity so that it ends up at `path`.
class AddEntityCommand : public Command {
public:
    AddEntityCommand(ProjectDocument& document, QString sceneId, EntityPath path, Entity entity);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    EntityPath m_path;
    Entity m_entity;
};

/// Removes entities, each with its children. A path inside another one is removed with it.
class RemoveEntitiesCommand : public Command {
public:
    RemoveEntitiesCommand(ProjectDocument& document, QString sceneId, QList<EntityPath> paths);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    std::vector<std::pair<EntityPath, Entity>> m_removed;  ///< In path order.
};

/// Changes entities' own fields (not their children): a drag of several, or an edit in the
/// inspector. Renaming an entity's id also renames it in the transitions that carry it
/// out of this scene or land it on a mark here.
class EditEntitiesCommand : public Command {
public:
    EditEntitiesCommand(ProjectDocument& document, QString sceneId,
                        QList<std::pair<EntityPath, Entity>> changes, QString text);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    QString m_text;
    std::vector<std::pair<EntityPath, Entity>> m_before;
    std::vector<std::pair<EntityPath, Entity>> m_after;
    std::vector<Transition> m_transitionsBefore;
    std::vector<Transition> m_transitionsAfter;
};

/// Moves an entity in the tree, to end at `to`, a path in the tree without the entity (see
/// pathAfterRemoval()): reordering, or a new parent. An entity
/// that becomes a child gives up its layer; one that becomes top-level keeps the layer it
/// was drawn on.
class MoveEntityCommand : public Command {
public:
    MoveEntityCommand(ProjectDocument& document, QString sceneId, EntityPath from, EntityPath to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    EntityPath m_from;
    EntityPath m_to;
    QString m_name;
    QString m_fromLayer;  ///< Its own `layer` before the move.
    QString m_toLayer;
};

/// Reorders the children of `parent` (the top level if empty), as arrangedOrders() gives:
/// `order[i]` is the index, before, of the entity that ends at index i.
class ReorderEntitiesCommand : public Command {
public:
    ReorderEntitiesCommand(ProjectDocument& document, QString sceneId, EntityPath parent,
                           QList<int> order, QString text);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    EntityPath m_parent;
    QList<int> m_order;
    QString m_text;
};

class AddLayerCommand : public Command {
public:
    AddLayerCommand(ProjectDocument& document, QString sceneId, int index, Layer layer);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    int m_index;
    Layer m_layer;
};

/// Removes a layer and the entities on it.
class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(ProjectDocument& document, QString sceneId, int index);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    int m_index;
    Layer m_layer;
    std::vector<std::pair<int, Entity>> m_entities;  ///< Top-level index -> entity.
};

/// Changes a layer's settings. A new id is carried to the entities on it.
class EditLayerCommand : public Command {
public:
    EditLayerCommand(ProjectDocument& document, QString sceneId, int index, Layer layer);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    /// Sets the layer, and its id on the entities that were on it.
    void apply(const Layer& layer);

    ProjectDocument& m_document;
    QString m_sceneId;
    int m_index;
    Layer m_before;
    Layer m_after;
    QList<int> m_entities;  ///< Top-level entities on the layer.
};

class MoveLayerCommand : public Command {
public:
    MoveLayerCommand(ProjectDocument& document, QString sceneId, int from, int to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_sceneId;
    int m_from;
    int m_to;
    QString m_name;
};

}  // namespace shoal::core
