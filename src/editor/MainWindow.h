#pragma once

#include <QList>
#include <QMainWindow>
#include <memory>

#include "core/ProjectStore.h"

class QAction;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class FlowScene;
class FlowView;

/// The editor's top-level window: one open project, shown in the flow view.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// The text of the About dialog: Shoal's version and the sprat-lib it is linked with.
    static QString aboutText();

    core::ProjectDocument* document() const { return m_document.get(); }
    FlowScene* flowScene() const { return m_flowScene.get(); }
    FlowView* flowView() const { return m_flowView; }

    /// Shows `document` (or nothing), replacing the current one without asking.
    void setDocument(std::unique_ptr<core::ProjectDocument> document);
    /// Opens the project in `directory` without asking or reporting; returns the errors,
    /// empty on success. On failure the current project stays open.
    QList<core::FileError> openProject(const QString& directory);
    /// Opens a project folder, or the folder of a project.shoal.json, and reports errors in
    /// a dialog.
    void openProjectAt(const QString& path);
    /// Saves into `directory` without asking or reporting.
    core::SaveResult saveProjectAs(const QString& directory);

public slots:
    void newProject();
    void openProjectDialog();
    /// Saves, asking for a folder if the project has none yet; false if not saved.
    bool save();
    bool saveAs();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    /// Offers to save unsaved changes; false if the user cancels.
    bool maybeSave();
    bool reportSave(const core::SaveResult& result);
    void updateTitle();
    void updateActions();
    void showAbout();

    std::unique_ptr<core::ProjectDocument> m_document;
    std::unique_ptr<FlowScene> m_flowScene;
    FlowView* m_flowView = nullptr;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QAction* m_addSceneAction = nullptr;
    QAction* m_renameSceneAction = nullptr;
    QAction* m_startSceneAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_fitAction = nullptr;
};

}  // namespace shoal::editor
