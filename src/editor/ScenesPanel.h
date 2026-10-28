#pragma once

#include <QPointer>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace shoal::editor {

class FlowScene;

/// The project's scenes, by id, for the flow: the start scene is marked, the selection is
/// the flow's, and activating a scene opens it.
class ScenesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScenesPanel(QWidget* parent = nullptr);

    /// Shows `flow`'s scenes; null shows nothing.
    void setFlowScene(FlowScene* flow);
    QListWidget* list() const { return m_list; }
    /// The row of a scene, or null.
    QListWidgetItem* sceneRow(const QString& sceneId) const;

signals:
    /// A scene was double-clicked or activated with the keyboard.
    void sceneActivated(const QString& sceneId);

private:
    void rebuild();
    /// Selects the rows of the flow's selected scenes.
    void showSelection();
    void onListSelectionChanged();

    QPointer<FlowScene> m_flow;
    QListWidget* m_list;
    bool m_syncing = false;
};

}  // namespace shoal::editor
