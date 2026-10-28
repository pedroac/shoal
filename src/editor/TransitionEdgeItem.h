#pragma once

#include <QGraphicsItem>
#include <QPainterPath>
#include <QString>

#include "core/Model.h"

namespace shoal::editor {

class FlowScene;

/// A transition between two scene nodes, drawn as an arrow labelled with its id. Edges
/// between the same two nodes bend apart; an edge from a node to itself loops above it.
class TransitionEdgeItem : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };

    TransitionEdgeItem(FlowScene& flowScene, QString transitionId);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString transitionId() const { return m_transitionId; }
    QString from() const { return m_from; }
    QString to() const { return m_to; }

    /// Sets what the edge connects and how. `bend` spreads edges between the same nodes: 0
    /// is a straight line, and each step bows the edge further to its left.
    void setTransition(const core::Transition& transition, double bend);
    /// Follows the nodes it connects; call when either one moves or resizes.
    void updateGeometry();

private:
    FlowScene& m_flowScene;
    QString m_transitionId;
    QString m_from;
    QString m_to;
    core::TransitionMode m_mode = core::TransitionMode::Replace;
    double m_bend = 0;

    QPainterPath m_path;
    QPolygonF m_arrow;
    QRectF m_labelRect;
};

}  // namespace shoal::editor
