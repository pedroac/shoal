#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <memory>
#include <optional>
#include <utility>

#include "assets/FontCache.h"
#include "assets/SheetCache.h"
#include "core/ProjectStore.h"
#include "core/SceneTree.h"

class QAction;
class QSplitter;
class QStackedWidget;
class QTabWidget;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class CanvasView;
class InspectorPanel;
class PresetsPanel;
class PresetsView;
class FlowScene;
class FlowView;
class SceneSettingsPanel;
class ScenesPanel;
class SceneTreePanel;
class SceneCanvas;
class SceneView;
class AssetBrowserPanel;
struct AssetItem;
class WorkspaceTabs;

/// The editor's top-level window: one open project, with the flow in the first tab, the
/// presets in the second, and a tab for each open scene. Under the tabs, between the side
/// panels, the current tab's workspace. On the left, the project panel: the project's
/// scenes on the flow, its presets and sheets on a scene or a preset. On the right, the context
/// panel: what is selected in the current tab, the selected scene's settings on the flow, the
/// entity tree and the inspector on a scene or a preset. Each side panel can be hidden
/// from the View menu.
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
    WorkspaceTabs* tabs() const { return m_tabs; }
    /// The left side panel: a tab each for the scenes, the presets and the sheets.
    QTabWidget* projectPanel() const { return m_projectPanel; }
    /// The right side panel: the scene settings, or the entity tree over the inspector.
    QStackedWidget* contextPanel() const { return m_contextPanel; }
    SceneTreePanel* sceneTreePanel() const { return m_sceneTree; }
    AssetBrowserPanel* assetBrowser() const { return m_assetBrowser; }
    InspectorPanel* inspector() const { return m_inspector; }
    PresetsPanel* presetsPanel() const { return m_presets; }
    ScenesPanel* scenesPanel() const { return m_scenes; }
    SceneSettingsPanel* sceneSettingsPanel() const { return m_sceneSettings; }
    assets::SheetCache& sheets() { return m_sheets; }
    assets::FontCache& fonts() { return m_fonts; }
    /// The Presets tab.
    PresetsView* presetsView() const { return m_presetsView; }
    /// The current tab's scene view, the current preset's in the Presets tab, or null
    /// when the flow tab is current.
    SceneView* currentSceneView() const;
    /// Opens a scene in its tab, or switches to the tab it already has.
    SceneView* openScene(const QString& sceneId);
    /// Switches to the Presets tab, showing `presetId` if it is given.
    PresetsView* openPresets(const QString& presetId = {});
    /// Makes a preset of the current scene's selection (see
    /// SceneCanvas::makePresetFromSelection()) and shows it; its id, or empty.
    QString makePresetFromSelection();

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
    /// Declares `files` in the manifest, in one undo step, each in the kind its extension
    /// says, under an alias made from its file name; files already declared, or of a kind
    /// Shoal doesn't know, are skipped. Paths are written
    /// relative to the project folder, or absolute while the project has none. Returns the
    /// new aliases.
    QStringList addAssets(const QStringList& files);

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
    /// Shows the selected entity's image in the sheets, for them to pick another one.
    void showAbout();
    CanvasView* currentCanvasView() const;
    /// The scenes' tabs.
    QList<SceneView*> sceneViews() const;
    void closeSceneTab(int index);
    void onCurrentTabChanged();
    /// Loads the sheets and images the manifest declares, relative to the project folder,
    /// and lists its other assets.
    void updateAssets();
    /// Places a sprite or animation from the sheet browser in the current scene's view.
    void placeAssetItem(const AssetItem& item);
    /// Places what a preset creates in the current scene's view.
    void placePreset(const QString& presetId);
    /// Points the panels that follow the current scene at `view`'s canvas, or nothing.
    void setPanelCanvas(SceneView* view);
    /// Shows the current tab's kind of context, and the project panel's lists for it.
    void updatePanels();

    std::unique_ptr<core::ProjectDocument> m_document;
    std::unique_ptr<FlowScene> m_flowScene;
    FlowView* m_flowView = nullptr;
    PresetsView* m_presetsView = nullptr;
    QPointer<SceneCanvas> m_presetCanvas;  ///< The presets tab's, connected to this window.
    WorkspaceTabs* m_tabs = nullptr;
    assets::SheetCache m_sheets;
    assets::FontCache m_fonts;
    core::Assets m_declaredAssets;
    QString m_assetsBase;
    QTabWidget* m_projectPanel = nullptr;
    QStackedWidget* m_contextPanel = nullptr;
    QSplitter* m_entityPanel = nullptr;  ///< The entity tree over the inspector.
    SceneTreePanel* m_sceneTree = nullptr;
    AssetBrowserPanel* m_assetBrowser = nullptr;
    PresetsPanel* m_presets = nullptr;
    InspectorPanel* m_inspector = nullptr;
    ScenesPanel* m_scenes = nullptr;
    SceneSettingsPanel* m_sceneSettings = nullptr;
    /// The kinds of tab, each with a context of its own.
    enum class TabKind { Flow, Scene, Presets };
    std::optional<TabKind> m_panelsFor;  ///< The kind of tab the panels show for.
    QPointer<QWidget> m_sceneList;       ///< The project panel's list last chosen on a scene.
    std::pair<int, int> m_sideWidths;    ///< The side panels' widths, kept between projects.

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QList<std::pair<core::Arrange, QAction*>> m_arrangeActions;
    QAction* m_addSceneAction = nullptr;
    QAction* m_renameSceneAction = nullptr;
    QAction* m_startSceneAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_projectPanelAction = nullptr;
    QAction* m_contextPanelAction = nullptr;
};

}  // namespace shoal::editor
