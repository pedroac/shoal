#pragma once

#include <QWidget>

class QSplitter;
class QStackedWidget;
class QTabBar;

namespace shoal::editor {

/// The workspaces as tabs across the window, each shown between the side panels: the
/// tab bar on top, and under it the project panel, the current workspace and the context
/// panel, side by side. The side panels are shared by every tab; only the page between
/// them changes. Its calls follow QTabWidget's.
class WorkspaceTabs : public QWidget {
    Q_OBJECT
public:
    explicit WorkspaceTabs(QWidget* parent = nullptr);

    /// Puts `left` and `right` on either side of the pages, at `widths` (left, right), the
    /// pages taking the rest.
    void setSidePanels(QWidget* left, QWidget* right, std::pair<int, int> widths);
    /// The side panels' widths, as the user left them.
    std::pair<int, int> sideWidths() const;

    int addTab(QWidget* page, const QString& text);
    /// Takes the tab's page out, without deleting it.
    void removeTab(int index);
    int count() const;
    QWidget* widget(int index) const;
    int indexOf(const QWidget* page) const;
    int currentIndex() const;
    QWidget* currentWidget() const;
    void setCurrentIndex(int index);
    void setCurrentWidget(QWidget* page);
    QString tabText(int index) const;
    void setTabText(int index, const QString& text);
    QTabBar* tabBar() const { return m_bar; }
    QSplitter* splitter() const { return m_splitter; }

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);

private:
    QTabBar* m_bar;
    QStackedWidget* m_pages;
    QSplitter* m_splitter;
};

}  // namespace shoal::editor
