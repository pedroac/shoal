#pragma once

#include <QGraphicsView>
#include <QPoint>
#include <QRectF>

namespace shoal::editor {

/// A view onto an editing canvas: Ctrl+wheel zooms around the cursor, the middle button
/// pans, and there is room to pan past the content.
class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);

    double zoom() const;

public slots:
    void zoomIn();
    void zoomOut();
    void resetZoom();
    /// Shows contentRect() whole, never magnified past 1.
    void fitAll();

protected:
    /// What fitAll() shows and the panning room surrounds; the items by default.
    virtual QRectF contentRect() const;
    void setZoom(double zoom);
    /// Keeps the panning room around `scene`'s content; for a scene set after construction.
    void watchScene(QGraphicsScene* scene);

    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateSceneRect();

    bool m_panning = false;
    QPoint m_panFrom;
};

}  // namespace shoal::editor
