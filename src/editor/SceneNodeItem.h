#pragma once

#include <QGraphicsItem>
#include <QString>
#include <QStringList>

namespace shoal::editor {

class FlowScene;

/// A scene in the flow view. Its position is the node's top-left corner, as stored in
/// flow.json. Dragging the node moves it; dragging from the port on its right edge draws a
/// new transition to the node it is dropped on.
class SceneNodeItem : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };
    static constexpr double WIDTH = 200;

    SceneNodeItem(FlowScene& flowScene, QString sceneId);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString sceneId() const { return m_sceneId; }
    void setSceneId(const QString& sceneId);

    /// What the node shows besides its id. Each change repaints it.
    bool isStart() const { return m_start; }
    void setStart(bool start);
    int modifierCount() const { return m_modifierCount; }
    void setModifierCount(int count);
    /// Transitions that lead to no scene node (`pop`, `push_modifier`, a missing scene), one
    /// line each, and the ids of those transitions. Clicking a line selects its transition.
    QStringList exits() const { return m_exits; }
    QStringList exitIds() const { return m_exitIds; }
    void setExits(const QStringList& exits, const QStringList& transitionIds);
    /// The line of the exit `transitionId`, in item coordinates; null if it has none.
    QRectF exitRect(const QString& transitionId) const;

    /// The rectangle of the node's body, in item coordinates.
    QRectF bodyRect() const;
    /// The port to drag transitions from, in item coordinates.
    QRectF portRect() const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void relayout();

    FlowScene& m_flowScene;
    QString m_sceneId;
    bool m_start = false;
    int m_modifierCount = 0;
    QStringList m_exits;
    QStringList m_exitIds;
    double m_height = 0;
    bool m_connecting = false;
    bool m_exitPressed = false;  ///< The press was on an exit's line, which it selected.
};

}  // namespace shoal::editor
