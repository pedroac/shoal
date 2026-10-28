#pragma once

#include <QMainWindow>

namespace shoal::editor {

/// The editor's top-level window. For now it holds only the menus and an empty workspace.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// The text of the About dialog: Shoal's version and the sprat-lib it is linked with.
    static QString aboutText();

private:
    void createMenus();
    void showAbout();
};

}  // namespace shoal::editor
