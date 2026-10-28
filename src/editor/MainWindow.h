#pragma once

#include <QList>
#include <QMainWindow>
#include <memory>

#include "assets/SheetCache.h"
#include "core/ProjectStore.h"

class QAction;
class QDockWidget;
class QTabWidget;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class CanvasView;
class ClassesPanel;
class InspectorPanel;
class PresetsPanel;
class FlowScene;
class FlowView;
class SceneTreePanel;
class SceneView;
class SheetBrowserPanel;
struct SheetItem;

/// The editor's top-level window: one open project, with the flow view in the first tab
/// and a tab for each open scene. The docks follow the current scene tab.
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
    QTabWidget* tabs() const { return m_tabs; }
    SceneTreePanel* sceneTreePanel() const { return m_sceneTree; }
    SheetBrowserPanel* sheetBrowser() const { return m_sheetBrowser; }
    InspectorPanel* inspector() const { return m_inspector; }
    ClassesPanel* classesPanel() const { return m_classes; }
    PresetsPanel* presetsPanel() const { return m_presets; }
    assets::SheetCache& sheets() { return m_sheets; }
    /// The current tab's scene view, or null when the flow tab is current.
    SceneView* currentSceneView() const;
    /// Opens a scene in its tab, or switches to the tab it already has.
    SceneView* openScene(const QString& sceneId);

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
    /// Declares the .sprat `files` in the manifest, in one undo step, each under an alias
    /// made from its file name; files already declared are skipped. Paths are written
    /// relative to the project folder, or absolute while the project has none. Returns the
    /// new aliases.
    QStringList addSheets(const QStringList& files);

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
    CanvasView* currentCanvasView() const;
    void closeSceneTab(int index);
    void onCurrentTabChanged();
    /// Loads the sheets the manifest declares, relative to the project folder.
    void updateSheets();
    /// Places a sprite or animation from the sheet browser in the current scene's view.
    void placeSheetItem(const SheetItem& item);
    /// Places what a preset creates in the current scene's view.
    void placePreset(const QString& presetId);
    /// Points the panels that follow the current scene at `view`'s canvas, or nothing.
    void setPanelCanvas(SceneView* view);
    QDockWidget* addDock(QWidget* panel, const QString& title, const QString& objectName,
                         Qt::DockWidgetArea area);

    std::unique_ptr<core::ProjectDocument> m_document;
    std::unique_ptr<FlowScene> m_flowScene;
    FlowView* m_flowView = nullptr;
    QTabWidget* m_tabs = nullptr;
    assets::SheetCache m_sheets;
    QMap<QString, QString> m_declaredSheets;
    QString m_sheetsBase;
    QDockWidget* m_sceneTreeDock = nullptr;
    SceneTreePanel* m_sceneTree = nullptr;
    QDockWidget* m_sheetBrowserDock = nullptr;
    SheetBrowserPanel* m_sheetBrowser = nullptr;
    QDockWidget* m_presetsDock = nullptr;
    PresetsPanel* m_presets = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    InspectorPanel* m_inspector = nullptr;
    QDockWidget* m_classesDock = nullptr;
    ClassesPanel* m_classes = nullptr;

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
    QAction* m_snapAction = nullptr;
};

}  // namespace shoal::editor
