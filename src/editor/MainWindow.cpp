#include "editor/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include "assets/SpratLibrary.h"
#include "core/AssetCommands.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "core/Version.h"
#include "editor/AssetBrowserPanel.h"
#include "editor/FlowScene.h"
#include "editor/FlowView.h"
#include "editor/InspectorPanel.h"
#include "editor/NewProjectDialog.h"
#include "editor/PresetsPanel.h"
#include "editor/PresetsView.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneSettingsPanel.h"
#include "editor/SceneTreePanel.h"
#include "editor/SceneView.h"
#include "editor/ScenesPanel.h"
#include "editor/WorkspaceTabs.h"

namespace shoal::editor {

namespace {
constexpr int DEFAULT_WIDTH = 1280;
constexpr int DEFAULT_HEIGHT = 800;
constexpr int STATUS_TIMEOUT_MS = 4000;
constexpr int PROJECT_PANEL_WIDTH = 240;
constexpr int CONTEXT_PANEL_WIDTH = 300;

QString describeErrors(const QList<core::FileError>& errors)
{
    QStringList lines;
    for (const core::FileError& error : errors) {
        lines << error.toString();
    }
    return lines.join(u'\n');
}

QAction* makeAction(QObject* parent, const QString& objectName, const QString& text,
                    const QList<QKeySequence>& shortcuts = {})
{
    auto* action = new QAction(text, parent);
    action->setObjectName(objectName);
    action->setShortcuts(shortcuts);
    return action;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // On the left, the project's scenes, presets and sheets; on the right, what is
    // selected in the current tab. Both go beside each project's tabs.
    m_scenes = new ScenesPanel(this);
    m_presets = new PresetsPanel(this);
    m_assetBrowser = new AssetBrowserPanel(m_sheets, m_fonts, this);
    m_projectPanel = new QTabWidget(this);
    m_projectPanel->setObjectName(QStringLiteral("projectPanel"));
    m_projectPanel->setDocumentMode(true);
    m_projectPanel->addTab(m_scenes, tr("Scenes"));
    m_projectPanel->addTab(m_presets, tr("Presets"));
    m_projectPanel->addTab(m_assetBrowser, tr("Assets"));
    m_sceneSettings = new SceneSettingsPanel(this);
    m_sceneTree = new SceneTreePanel(this);
    m_inspector = new InspectorPanel(this);
    m_entityPanel = new QSplitter(Qt::Vertical, this);
    m_entityPanel->setObjectName(QStringLiteral("entityPanel"));
    m_entityPanel->addWidget(m_sceneTree);
    m_entityPanel->addWidget(m_inspector);
    m_entityPanel->setStretchFactor(0, 2);
    m_entityPanel->setStretchFactor(1, 3);
    m_contextPanel = new QStackedWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("contextPanel"));
    m_contextPanel->addWidget(m_sceneSettings);
    m_contextPanel->addWidget(m_entityPanel);
    m_sideWidths = {PROJECT_PANEL_WIDTH, CONTEXT_PANEL_WIDTH};
    connect(m_presets, &PresetsPanel::currentPresetChanged, this, [this](const QString& id) {
        if (m_presetsView) {
            m_presetsView->showPreset(id);
        }
    });
    connect(m_presets, &PresetsPanel::editRequested, this,
            [this](const QString& id) { openPresets(id); });
    connect(m_scenes, &ScenesPanel::sceneActivated, this, &MainWindow::openScene);
    connect(m_assetBrowser, &AssetBrowserPanel::placeRequested, this, &MainWindow::placeAssetItem);
    connect(m_presets, &PresetsPanel::placeRequested, this, &MainWindow::placePreset);
    connect(m_assetBrowser, &AssetBrowserPanel::assetFilesDropped, this, &MainWindow::addAssets);
    createActions();
    createMenus();
    setDocument(nullptr);
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow()
{
    // Close the project while the window is whole: QWidget deletes the view, a child, only
    // after the scene and document it shows are gone.
    setDocument(nullptr);
}

QString MainWindow::aboutText()
{
    return tr("<h3>Shoal %1</h3>"
              "<p>A visual scene graph and scene flow editor for custom game engines.</p>"
              "<p>sprat-lib %2</p>"
              "<p>MIT License</p>")
        .arg(core::version(), assets::spratLibraryVersion());
}

void MainWindow::createActions()
{
    m_newAction =
        makeAction(this, QStringLiteral("newAction"), tr("&New Project…"), {QKeySequence::New});
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);
    m_openAction =
        makeAction(this, QStringLiteral("openAction"), tr("&Open Project…"), {QKeySequence::Open});
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProjectDialog);
    m_saveAction =
        makeAction(this, QStringLiteral("saveAction"), tr("&Save"), {QKeySequence::Save});
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::save);
    m_saveAsAction =
        makeAction(this, QStringLiteral("saveAsAction"), tr("Save &As…"), {QKeySequence::SaveAs});
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);
    m_quitAction =
        makeAction(this, QStringLiteral("quitAction"), tr("&Quit"), {QKeySequence::Quit});
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction =
        makeAction(this, QStringLiteral("undoAction"), tr("&Undo"), {QKeySequence::Undo});
    connect(m_undoAction, &QAction::triggered, this, [this] {
        if (m_document) {
            m_document->commands().undo();
        }
    });
    m_redoAction =
        makeAction(this, QStringLiteral("redoAction"), tr("&Redo"), {QKeySequence::Redo});
    connect(m_redoAction, &QAction::triggered, this, [this] {
        if (m_document) {
            m_document->commands().redo();
        }
    });
    // Backspace too, as on macOS, where it is already among Delete's bindings.
    QList<QKeySequence> deleteKeys = QKeySequence::keyBindings(QKeySequence::Delete);
    if (!deleteKeys.contains(QKeySequence(Qt::Key_Backspace))) {
        deleteKeys << QKeySequence(Qt::Key_Backspace);
    }
    m_deleteAction = makeAction(this, QStringLiteral("deleteAction"), tr("&Delete"), deleteKeys);
    connect(m_deleteAction, &QAction::triggered, this, [this] {
        if (SceneView* view = currentSceneView()) {
            view->canvas().removeSelection();
        }
        else if (m_flowScene && m_tabs->currentWidget() == m_flowView) {
            m_flowScene->removeSelection();
        }
    });
    m_selectAllAction = makeAction(this, QStringLiteral("selectAllAction"), tr("Select &All"),
                                   {QKeySequence::SelectAll});
    connect(m_selectAllAction, &QAction::triggered, this, [this] {
        if (CanvasView* view = currentCanvasView()) {
            for (QGraphicsItem* item : view->scene()->items()) {
                item->setSelected(item->flags() & QGraphicsItem::ItemIsSelectable);
            }
        }
    });

    const std::pair<core::Arrange, const char*> arrangeNames[] = {
        {core::Arrange::ToFront, "bringToFrontAction"},
        {core::Arrange::Forward, "bringForwardAction"},
        {core::Arrange::Backward, "sendBackwardAction"},
        {core::Arrange::ToBack, "sendToBackAction"},
    };
    for (const auto& [arrange, name] : arrangeNames) {
        QAction* action =
            makeAction(this, QLatin1StringView(name), SceneCanvas::arrangeText(arrange),
                       {SceneCanvas::arrangeShortcut(arrange)});
        connect(action, &QAction::triggered, this, [this, arrange = arrange] {
            if (SceneView* view = currentSceneView()) {
                view->canvas().arrangeSelection(arrange);
            }
        });
        m_arrangeActions.append({arrange, action});
    }

    m_addSceneAction = makeAction(this, QStringLiteral("addSceneAction"), tr("&Add Scene"));
    connect(m_addSceneAction, &QAction::triggered, this, [this] {
        if (m_flowView) {
            m_flowView->addScene();
        }
    });
    m_renameSceneAction = makeAction(this, QStringLiteral("renameSceneAction"),
                                     tr("&Rename Scene…"), {QKeySequence(Qt::Key_F2)});
    connect(m_renameSceneAction, &QAction::triggered, this, [this] {
        if (m_flowView) {
            m_flowView->renameSelectedScene();
        }
    });
    m_startSceneAction =
        makeAction(this, QStringLiteral("startSceneAction"), tr("Set as &Start Scene"));
    connect(m_startSceneAction, &QAction::triggered, this, [this] {
        if (m_flowView) {
            m_flowView->setSelectedSceneAsStart();
        }
    });

    m_zoomInAction =
        makeAction(this, QStringLiteral("zoomInAction"), tr("Zoom &In"), {QKeySequence::ZoomIn});
    m_zoomOutAction =
        makeAction(this, QStringLiteral("zoomOutAction"), tr("Zoom &Out"), {QKeySequence::ZoomOut});
    m_actualSizeAction = makeAction(this, QStringLiteral("actualSizeAction"), tr("&Actual Size"),
                                    {QKeySequence(Qt::CTRL | Qt::Key_1)});
    m_fitAction = makeAction(this, QStringLiteral("fitAction"), tr("&Fit to Window"),
                             {QKeySequence(Qt::CTRL | Qt::Key_0)});
    const std::pair<QAction*, void (CanvasView::*)()> viewActions[] = {
        {m_zoomInAction, &CanvasView::zoomIn},
        {m_zoomOutAction, &CanvasView::zoomOut},
        {m_actualSizeAction, &CanvasView::resetZoom},
        {m_fitAction, &CanvasView::fitAll},
    };
    for (const auto& [action, slot] : viewActions) {
        connect(action, &QAction::triggered, this, [this, slot = slot] {
            if (CanvasView* view = currentCanvasView()) {
                (view->*slot)();
            }
        });
    }
    m_snapAction = makeAction(this, QStringLiteral("snapAction"), tr("&Snap to Grid"),
                              {QKeySequence(Qt::CTRL | Qt::Key_Apostrophe)});
    m_snapAction->setCheckable(true);
    m_projectPanelAction =
        makeAction(this, QStringLiteral("projectPanelAction"), tr("&Project Panel"),
                   {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1)});
    m_contextPanelAction =
        makeAction(this, QStringLiteral("contextPanelAction"), tr("&Context Panel"),
                   {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2)});
    for (auto [action, panel] :
         {std::pair<QAction*, QWidget*>{m_projectPanelAction, m_projectPanel},
          {m_contextPanelAction, m_contextPanel}}) {
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, panel = panel](bool shown) {
            if (m_tabs) {
                panel->setVisible(shown);
            }
        });
    }
    connect(m_snapAction, &QAction::toggled, this, [this](bool snapping) {
        for (SceneView* view : sceneViews()) {
            view->canvas().setSnapping(snapping);
        }
        if (m_presetsView) {
            m_presetsView->setSnapping(snapping);
        }
    });
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_deleteAction);
    editMenu->addAction(m_selectAllAction);

    QMenu* arrangeMenu = menuBar()->addMenu(tr("&Arrange"));
    for (const auto& [arrange, action] : m_arrangeActions) {
        arrangeMenu->addAction(action);
    }

    QMenu* flowMenu = menuBar()->addMenu(tr("F&low"));
    flowMenu->addAction(m_addSceneAction);
    flowMenu->addAction(m_renameSceneAction);
    flowMenu->addAction(m_startSceneAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_snapAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_projectPanelAction);
    viewMenu->addAction(m_contextPanelAction);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Shoal"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::setDocument(std::unique_ptr<core::ProjectDocument> document)
{
    // Tear down the views first: they show the old document. Its scenes emit
    // selectionChanged() as they go, which must no longer reach this window.
    setPanelCanvas(nullptr);
    m_scenes->setFlowScene(nullptr);
    m_sceneSettings->setFlowScene(nullptr);
    m_inspector->setFlowScene(nullptr);
    m_presets->setDocument(nullptr);
    if (m_tabs) {
        disconnect(m_tabs, nullptr, this, nullptr);
        for (SceneView* view : sceneViews()) {
            disconnect(&view->canvas(), nullptr, this, nullptr);
        }
        if (m_presetsView) {
            disconnect(m_presetsView, nullptr, this, nullptr);
        }
        if (m_presetCanvas) {
            disconnect(m_presetCanvas, nullptr, this, nullptr);
        }
    }
    if (m_tabs) {
        // The side panels outlive the project's tabs.
        m_sideWidths = m_tabs->sideWidths();
        for (QWidget* panel :
             {static_cast<QWidget*>(m_projectPanel), static_cast<QWidget*>(m_contextPanel)}) {
            panel->hide();
            panel->setParent(this);
        }
    }
    delete takeCentralWidget();
    m_tabs = nullptr;
    m_panelsFor.reset();
    m_flowView = nullptr;
    m_presetsView = nullptr;
    if (m_flowScene) {
        disconnect(m_flowScene.get(), nullptr, this, nullptr);
    }
    m_flowScene.reset();
    m_document = std::move(document);
    updateAssets();
    m_presets->setDocument(m_document.get());

    if (m_document) {
        m_flowScene = std::make_unique<FlowScene>(*m_document);
        m_scenes->setFlowScene(m_flowScene.get());
        m_sceneSettings->setFlowScene(m_flowScene.get());
        m_inspector->setFlowScene(m_flowScene.get());
        m_tabs = new WorkspaceTabs(this);
        m_tabs->setObjectName(QStringLiteral("workspaceTabs"));
        m_tabs->tabBar()->setTabsClosable(true);
        m_tabs->tabBar()->setMovable(true);
        m_tabs->setSidePanels(m_projectPanel, m_contextPanel, m_sideWidths);
        m_projectPanel->setVisible(m_projectPanelAction->isChecked());
        m_contextPanel->setVisible(m_contextPanelAction->isChecked());
        m_flowView = new FlowView(*m_flowScene, m_tabs);
        m_flowView->setObjectName(QStringLiteral("flowView"));
        m_tabs->addTab(m_flowView, tr("Flow"));
        m_presetsView = new PresetsView(*m_document, m_sheets, m_fonts, m_tabs);
        m_presetsView->setObjectName(QStringLiteral("presetsView"));
        m_presetsView->setSnapping(m_snapAction->isChecked());
        m_tabs->addTab(m_presetsView, tr("Presets"));
        for (int i = 0; i < 2; ++i) {  // neither is closable
            m_tabs->tabBar()->setTabButton(i, QTabBar::RightSide, nullptr);
            m_tabs->tabBar()->setTabButton(i, QTabBar::LeftSide, nullptr);
        }
        connect(m_presetsView, &PresetsView::viewChanged, this, [this] {
            if (m_tabs->currentWidget() == m_presetsView) {
                onCurrentTabChanged();
            }
            // The canvas it replaced may still be going.
            if (m_presetCanvas) {
                disconnect(m_presetCanvas, nullptr, this, nullptr);
            }
            m_presetCanvas = m_presetsView->view() ? &m_presetsView->view()->canvas() : nullptr;
            if (m_presetCanvas) {
                connect(m_presetCanvas, &QGraphicsScene::selectionChanged, this,
                        &MainWindow::updateActions);
                connect(m_presetCanvas, &SceneCanvas::presetMade, this,
                        [this](const QString& id) { openPresets(id); });
            }
        });
        m_presetsView->showPreset(m_presets->currentPreset());
        setCentralWidget(m_tabs);
        connect(m_tabs, &WorkspaceTabs::tabCloseRequested, this, &MainWindow::closeSceneTab);
        connect(m_tabs, &WorkspaceTabs::currentChanged, this, &MainWindow::onCurrentTabChanged);

        core::CommandStack& commands = m_document->commands();
        connect(&commands, &core::CommandStack::indexChanged, this, &MainWindow::updateActions);
        connect(&commands, &core::CommandStack::cleanChanged, this, &MainWindow::updateTitle);
        connect(m_flowScene.get(), &QGraphicsScene::selectionChanged, this,
                &MainWindow::updateActions);
        connect(m_document.get(), &core::ProjectDocument::startSceneChanged, this,
                &MainWindow::updateActions);
        connect(m_document.get(), &core::ProjectDocument::changed, this, &MainWindow::updateAssets);
        connect(m_flowScene.get(), &FlowScene::sceneActivated, this, &MainWindow::openScene);
        connect(m_document.get(), &core::ProjectDocument::sceneRemoved, this,
                [this](const QString& id) {
                    for (SceneView* view : sceneViews()) {
                        if (view->canvas().sceneId() == id) {
                            closeSceneTab(m_tabs->indexOf(view));
                        }
                    }
                });
        connect(m_document.get(), &core::ProjectDocument::sceneRenamed, this,
                [this](const QString& from, const QString& to) {
                    for (SceneView* view : sceneViews()) {
                        if (view->canvas().sceneId() == from) {
                            m_tabs->setTabText(m_tabs->indexOf(view), to);
                        }
                    }
                });
        // Once the window has its final size.
        QTimer::singleShot(0, m_flowView, &FlowView::fitAll);
    }
    else {
        auto* placeholder = new QLabel(tr("No project open"), this);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setObjectName(QStringLiteral("workspacePlaceholder"));
        setCentralWidget(placeholder);
    }
    updateTitle();
    updateActions();
    updatePanels();
}

SceneView* MainWindow::currentSceneView() const
{
    if (!m_tabs) {
        return nullptr;
    }
    if (m_tabs->currentWidget() == m_presetsView) {
        return m_presetsView->view();
    }
    return qobject_cast<SceneView*>(m_tabs->currentWidget());
}

CanvasView* MainWindow::currentCanvasView() const
{
    if (SceneView* view = currentSceneView()) {
        return view;
    }
    return m_tabs ? qobject_cast<CanvasView*>(m_tabs->currentWidget()) : nullptr;
}

QList<SceneView*> MainWindow::sceneViews() const
{
    QList<SceneView*> views;
    for (int i = 0; m_tabs && i < m_tabs->count(); ++i) {
        if (auto* view = qobject_cast<SceneView*>(m_tabs->widget(i))) {
            views << view;
        }
    }
    return views;
}

PresetsView* MainWindow::openPresets(const QString& presetId)
{
    if (!m_presetsView) {
        return nullptr;
    }
    m_tabs->setCurrentWidget(m_presetsView);
    if (!presetId.isEmpty()) {
        m_presets->setCurrentPreset(presetId);  // which the tab shows
    }
    return m_presetsView;
}

QString MainWindow::makePresetFromSelection()
{
    SceneView* view = currentSceneView();
    return view ? view->canvas().makePresetFromSelection() : QString();  // shown on presetMade
}

SceneView* MainWindow::openScene(const QString& sceneId)
{
    if (!m_document || !m_document->project().scenes.contains(sceneId)) {
        return nullptr;
    }
    for (SceneView* view : sceneViews()) {
        if (view->canvas().sceneId() == sceneId) {
            m_tabs->setCurrentWidget(view);
            return view;
        }
    }
    auto* view = new SceneView(*m_document, m_sheets, m_fonts, sceneId, m_tabs);
    view->setObjectName(QStringLiteral("sceneView"));
    view->canvas().setSnapping(m_snapAction->isChecked());
    connect(&view->canvas(), &QGraphicsScene::selectionChanged, this, &MainWindow::updateActions);
    connect(&view->canvas(), &SceneCanvas::presetMade, this,
            [this](const QString& id) { openPresets(id); });
    m_tabs->setCurrentIndex(m_tabs->addTab(view, sceneId));
    QTimer::singleShot(0, view, &CanvasView::fitAll);
    return view;
}

void MainWindow::closeSceneTab(int index)
{
    auto* view = m_tabs ? qobject_cast<SceneView*>(m_tabs->widget(index)) : nullptr;
    if (!view) {
        return;  // the flow and presets tabs stay
    }
    disconnect(&view->canvas(), nullptr, this, nullptr);
    if (m_sceneTree->canvas() == &view->canvas()) {
        setPanelCanvas(nullptr);
    }
    m_tabs->removeTab(index);
    view->deleteLater();
}

void MainWindow::onCurrentTabChanged()
{
    setPanelCanvas(currentSceneView());
    updateActions();
    updatePanels();
}

void MainWindow::setPanelCanvas(SceneView* view)
{
    SceneCanvas* canvas = view ? &view->canvas() : nullptr;
    m_sceneTree->setCanvas(canvas);
    m_inspector->setCanvas(canvas);
}

void MainWindow::updatePanels()
{
    if (!m_tabs) {
        return;
    }
    // The presets tab is edited as a scene is, with or without a preset.
    const TabKind kind = m_tabs->currentWidget() == m_flowView      ? TabKind::Flow
                         : m_tabs->currentWidget() == m_presetsView ? TabKind::Presets
                                                                    : TabKind::Scene;
    if (m_panelsFor == kind) {
        return;
    }
    if (m_panelsFor == TabKind::Scene) {
        m_sceneList = m_projectPanel->currentWidget();
    }
    m_panelsFor = kind;
    m_contextPanel->setCurrentWidget(kind == TabKind::Flow ? static_cast<QWidget*>(m_sceneSettings)
                                                           : m_entityPanel);
    // The lists the tab works with: the scenes on the flow, what to place on a scene or a
    // preset. A scene comes back to the one the user chose last on a scene.
    QWidget* current = kind == TabKind::Flow      ? static_cast<QWidget*>(m_scenes)
                       : kind == TabKind::Presets ? static_cast<QWidget*>(m_presets)
                       : m_sceneList              ? m_sceneList.data()
                                                  : m_assetBrowser;
    // Shown before the others are hidden, so hiding the current one doesn't pick another.
    m_projectPanel->setTabVisible(m_projectPanel->indexOf(current), true);
    m_projectPanel->setCurrentWidget(current);
    const bool flow = kind == TabKind::Flow;
    m_projectPanel->setTabVisible(m_projectPanel->indexOf(m_scenes), flow);
    m_projectPanel->setTabVisible(m_projectPanel->indexOf(m_presets), !flow);
    m_projectPanel->setTabVisible(m_projectPanel->indexOf(m_assetBrowser), !flow);
}

void MainWindow::updateAssets()
{
    core::Assets declared;
    QString base;
    if (m_document) {
        declared = m_document->project().manifest.assets;
        base = m_document->directory().isEmpty() ? QDir::currentPath() : m_document->directory();
    }
    if (declared != m_declaredAssets || base != m_assetsBase) {
        m_declaredAssets = declared;
        m_assetsBase = base;
        m_sheets.setSheets(declared.sheets, base, declared.images);
        m_fonts.setFonts(declared.fonts, base);
        m_assetBrowser->setAssets(declared, base);
    }
}

QStringList MainWindow::addAssets(const QStringList& files)
{
    QStringList added;
    if (!m_document) {
        statusBar()->showMessage(tr("Open a project to add assets to it."), STATUS_TIMEOUT_MS);
        return added;
    }
    const QString directory = m_document->directory();
    const QDir base(m_assetsBase);  // what the caches resolve paths against
    QStringList declared;
    const core::Assets& assets = m_document->project().manifest.assets;
    for (core::AssetKind kind : core::Assets::KINDS) {
        for (const QString& file : assets.of(kind)) {
            declared << QFileInfo(base.filePath(file)).absoluteFilePath();
        }
    }

    core::CommandStack& commands = m_document->commands();
    commands.beginMacro(files.size() == 1 ? tr("Add asset") : tr("Add assets"));
    for (const QString& file : files) {
        const QFileInfo info(file);
        const QString absolute = info.absoluteFilePath();
        const std::optional<core::AssetKind> kind = core::assetKindForFile(info.fileName());
        if (!kind || declared.contains(absolute)) {
            continue;
        }
        declared << absolute;
        const QString path = directory.isEmpty() ? absolute : base.relativeFilePath(absolute);
        const QString alias = m_document->uniqueAssetAlias(info.completeBaseName());
        commands.push(std::make_unique<core::SetAssetCommand>(*m_document, *kind, alias, path));
        added << alias;
    }
    commands.endMacro();

    statusBar()->showMessage(added.isEmpty() ? tr("These files are already in the project.")
                                             : tr("Added %1").arg(added.join(QStringLiteral(", "))),
                             STATUS_TIMEOUT_MS);
    return added;
}

void MainWindow::placeAssetItem(const AssetItem& item)
{
    SceneView* view = currentSceneView();
    if (!view) {
        const QString name = item.kind == AssetItem::Kind::Image ? item.alias : item.name;
        statusBar()->showMessage(tr("Open a scene to place %1 in it.").arg(name),
                                 STATUS_TIMEOUT_MS);
        return;
    }
    if (view->placeAtCenter(item).isEmpty()) {
        statusBar()->showMessage(tr("The current parent is hidden, locked or in screen space."),
                                 STATUS_TIMEOUT_MS);
    }
}

void MainWindow::placePreset(const QString& presetId)
{
    if (m_tabs && m_tabs->currentWidget() == m_presetsView) {
        return;  // there, choosing a preset edits it; dragging one places it
    }
    SceneView* view = currentSceneView();
    if (!view) {
        statusBar()->showMessage(tr("Open a scene to place %1 in it.").arg(presetId),
                                 STATUS_TIMEOUT_MS);
        return;
    }
    QString error;
    if (view->placePreset(presetId, view->visibleCenter(), &error).isEmpty()) {
        statusBar()->showMessage(
            error.isEmpty() ? tr("The current parent is hidden, locked or in screen space.")
                            : tr("Can't read the preset %1: %2").arg(presetId, error),
            STATUS_TIMEOUT_MS);
    }
}

QList<core::FileError> MainWindow::openProject(const QString& directory)
{
    core::ProjectDocument::OpenResult opened = core::ProjectDocument::open(directory);
    if (!opened.document) {
        return opened.errors;
    }
    setDocument(std::move(opened.document));
    return {};
}

void MainWindow::openProjectAt(const QString& path)
{
    const QFileInfo info(path);
    const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    const QList<core::FileError> errors = openProject(directory);
    if (!errors.isEmpty()) {
        QMessageBox box(
            QMessageBox::Critical, tr("Open Project"),
            tr("The project in %1 can't be opened.").arg(QDir::toNativeSeparators(directory)),
            QMessageBox::Ok, this);
        box.setInformativeText(tr("Fix the files below and open it again. Nothing was changed."));
        box.setDetailedText(describeErrors(errors));
        box.exec();
        return;
    }
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(directory)),
                             STATUS_TIMEOUT_MS);
}

core::SaveResult MainWindow::saveProjectAs(const QString& directory)
{
    Q_ASSERT(m_document);
    core::SaveResult result = m_document->saveAs(directory);
    updateAssets();  // relative to the new folder
    updateTitle();
    return result;
}

void MainWindow::newProject()
{
    if (!maybeSave()) {
        return;
    }
    NewProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    setDocument(std::make_unique<core::ProjectDocument>(
        core::newProject(dialog.projectName(), dialog.projectTemplate())));
}

void MainWindow::openProjectDialog()
{
    if (!maybeSave()) {
        return;
    }
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Open Project"), m_document ? m_document->directory() : QString(),
        tr("Shoal projects (project.shoal.json);;All files (*)"));
    if (!file.isEmpty()) {
        openProjectAt(file);
    }
}

bool MainWindow::save()
{
    if (!m_document) {
        return false;
    }
    if (m_document->directory().isEmpty()) {
        return saveAs();
    }
    const core::SaveResult result = m_document->save();
    updateTitle();
    return reportSave(result);
}

bool MainWindow::saveAs()
{
    if (!m_document) {
        return false;
    }
    const QString directory =
        QFileDialog::getExistingDirectory(this, tr("Save Project In"), m_document->directory());
    if (directory.isEmpty()) {
        return false;
    }
    const bool elsewhere = QDir(directory) != QDir(m_document->directory());
    if (elsewhere && QFileInfo::exists(QDir(directory).filePath(core::paths::MANIFEST))) {
        const auto answer = QMessageBox::warning(
            this, tr("Save Project"),
            tr("%1 already holds a project. Replace it?").arg(QDir::toNativeSeparators(directory)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }
    return reportSave(saveProjectAs(directory));
}

bool MainWindow::reportSave(const core::SaveResult& result)
{
    if (!result.ok()) {
        QMessageBox box(QMessageBox::Critical, tr("Save Project"),
                        tr("Some files couldn't be saved."), QMessageBox::Ok, this);
        box.setInformativeText(
            tr("The files that did save are complete, and the others are as they were."));
        box.setDetailedText(describeErrors(result.errors));
        box.exec();
        return false;
    }
    statusBar()->showMessage(
        result.written.isEmpty() && result.removed.isEmpty()
            ? tr("Saved; nothing had changed")
            : tr("Saved %1").arg(QDir::toNativeSeparators(m_document->directory())),
        STATUS_TIMEOUT_MS);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_document || m_document->commands().isClean()) {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("Save the changes to %1?").arg(m_document->project().manifest.name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return save();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        event->accept();
    }
    else {
        event->ignore();
    }
}

void MainWindow::updateTitle()
{
    if (!m_document) {
        setWindowTitle(QStringLiteral("Shoal"));
        setWindowModified(false);
        return;
    }
    QString name = m_document->project().manifest.name;
    if (name.isEmpty()) {
        name = QFileInfo(m_document->directory()).fileName();
    }
    setWindowTitle(tr("%1[*] — Shoal").arg(name));
    setWindowModified(!m_document->commands().isClean());
}

void MainWindow::updateActions()
{
    const bool open = m_document != nullptr;
    for (QAction* action : {m_saveAction, m_saveAsAction, m_selectAllAction, m_addSceneAction,
                            m_zoomInAction, m_zoomOutAction, m_actualSizeAction, m_fitAction}) {
        action->setEnabled(open);
    }

    const core::CommandStack* commands = open ? &m_document->commands() : nullptr;
    const bool canUndo = commands && commands->canUndo();
    const bool canRedo = commands && commands->canRedo();
    m_undoAction->setEnabled(canUndo);
    m_undoAction->setText(canUndo ? tr("&Undo %1").arg(commands->undoText()) : tr("&Undo"));
    m_redoAction->setEnabled(canRedo);
    m_redoAction->setText(canRedo ? tr("&Redo %1").arg(commands->redoText()) : tr("&Redo"));

    const bool flow = open && m_tabs && m_tabs->currentWidget() == m_flowView;
    m_addSceneAction->setEnabled(flow);
    const QStringList scenes = flow ? m_flowScene->selectedSceneIds() : QStringList();
    const QStringList transitions = flow ? m_flowScene->selectedTransitionIds() : QStringList();
    const bool oneScene = scenes.size() == 1 && transitions.isEmpty();
    m_renameSceneAction->setEnabled(oneScene);
    m_startSceneAction->setEnabled(oneScene &&
                                   m_document->project().manifest.start != scenes.first());
    SceneView* view = currentSceneView();
    const core::Scene* scene = view ? view->canvas().scene() : nullptr;
    const QList<core::EntityPath> selected =
        view ? view->canvas().selectedPaths() : QList<core::EntityPath>();
    for (const auto& [arrange, action] : m_arrangeActions) {
        action->setEnabled(scene && !core::arrangedOrders(*scene, selected, arrange).isEmpty());
    }
    if (view) {
        m_deleteAction->setEnabled(!selected.isEmpty());
    }
    else {
        m_deleteAction->setEnabled(!scenes.isEmpty() || !transitions.isEmpty());
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Shoal"), aboutText());
}

}  // namespace shoal::editor
