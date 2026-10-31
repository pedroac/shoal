#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <optional>

#include "core/Model.h"

namespace shoal::core {
class ProjectDocument;
}

class QGraphicsPathItem;

namespace shoal::editor {

class SceneNodeItem;
class TransitionEdgeItem;

/// The flow graph of a document: a node per scene and an edge per transition between two
/// scenes. It follows the document's signals, so undo and redo show up here, and turns what
/// the user does into commands on the document's stack.
///
/// A scene without a position in flow.json is placed below the others. That placement is
/// the view's alone: the project stays unchanged until the user moves the node.
class FlowScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit FlowScene(core::ProjectDocument& document, QObject* parent = nullptr);
    ~FlowScene() override;

    core::ProjectDocument& document() const { return m_document; }

    SceneNodeItem* node(const QString& sceneId) const;
    TransitionEdgeItem* edge(const QString& transitionId) const;
    /// The node or edge at `position`, nodes first; null over empty space.
    QGraphicsItem* itemAt(QPointF position) const;

    QStringList selectedSceneIds() const;
    /// The selected edges' transitions, and the selected exit's.
    QStringList selectedTransitionIds() const;
    /// The selected transition that has no edge, listed on its node as an exit, or empty.
    QString selectedExit() const { return m_selectedExit; }
    /// Selects exactly these scenes and transitions; a transition without an edge, the first
    /// one, is selected as its node's exit.
    void select(const QStringList& sceneIds, const QStringList& transitionIds = {});

    // --- edits, each one undo step ---

    /// Adds a new scene with its node's top-left corner at `position`; returns its id.
    QString addScene(QPointF position);
    /// Removes the selected transitions and scenes (a scene with its transitions).
    void removeSelection();
    /// Renames a scene; false if `to` isn't a valid id or another scene has it.
    bool renameScene(const QString& from, const QString& to);
    /// Why renameScene() refused `to`, for the user.
    static QString renameRefusal(const QString& to);
    void setStartScene(const QString& sceneId);
    /// Sets a scene's size, and the fit that overrides the project's (null for none).
    void setSceneSettings(const QString& sceneId, core::Size size, std::optional<core::Fit> fit);
    /// Adds a `replace` transition with no trigger yet; returns its id.
    QString addTransition(const QString& from, const QString& to);
    /// Replaces the transition `transitionId` with `transition`, as `text` on the undo stack,
    /// and keeps it selected if its id changes; false, changing nothing, if its new id isn't
    /// valid or another transition has it. Nothing if it is the same.
    bool setTransition(const QString& transitionId, const core::Transition& transition,
                       const QString& text);
    /// Why setTransition() refused the id `to`, for the user.
    static QString transitionRenameRefusal(const QString& to);
    /// Declares the state key `key` in the flow, and has the transition carry it, in one
    /// step; false if it isn't a key, or the flow already declares it.
    bool declareStateKey(const QString& transitionId, const QString& key);
    /// Sets a transition's effect, or takes it away (null); nothing if it is the same.
    void setTransitionEffect(const QString& transitionId,
                             const std::optional<core::Effect>& effect);

    // --- for the items ---

    /// Pushes one command for the nodes the user dragged, rounded to whole units so
    /// flow.json stays tidy. A click that moved nothing pushes nothing.
    void commitNodeMoves();
    void handleNodeMoved(const SceneNodeItem& node);
    void beginConnection(const SceneNodeItem& from, QPointF position);
    void updateConnection(QPointF position);
    void finishConnection(QPointF position);

signals:
    /// A scene node was double-clicked.
    void sceneActivated(const QString& sceneId);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void onSceneRenamed(const QString& from, const QString& to);
    void setSelectedExit(const QString& transitionId);
    /// Brings the items in line with the document.
    void sync();
    void syncNodes();
    void syncEdges();
    QPointF storedPosition(const QString& sceneId) const;
    void placeUnpositionedNodes();

    core::ProjectDocument& m_document;
    QHash<QString, SceneNodeItem*> m_nodes;
    QHash<QString, TransitionEdgeItem*> m_edges;
    QHash<QString, QPointF> m_placed;  ///< Where nodes without a stored position are shown.
    /// A transition without an edge, selected on its node's line. Selecting anything else, or
    /// clicking elsewhere, unselects it.
    QString m_selectedExit;
    bool m_selecting = false;  ///< In select().
    QGraphicsPathItem* m_connection = nullptr;
    QString m_connectionFrom;
};

}  // namespace shoal::editor
