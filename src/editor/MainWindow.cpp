#include "editor/MainWindow.h"

#include <QApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include "assets/SpratLibrary.h"
#include "core/Version.h"

namespace shoal::editor {

namespace {
constexpr int DEFAULT_WIDTH = 1280;
constexpr int DEFAULT_HEIGHT = 800;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Shoal"));
    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);

    auto* placeholder = new QLabel(tr("No project open"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setObjectName(QStringLiteral("workspacePlaceholder"));
    setCentralWidget(placeholder);

    createMenus();
    statusBar()->showMessage(tr("Ready"));
}

QString MainWindow::aboutText()
{
    return tr("<h3>Shoal %1</h3>"
              "<p>A visual scene graph and scene flow editor for custom game engines.</p>"
              "<p>sprat-lib %2</p>"
              "<p>MIT License</p>")
        .arg(core::version(), assets::spratLibraryVersion());
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Shoal"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Shoal"), aboutText());
}

}  // namespace shoal::editor
