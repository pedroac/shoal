#include "editor/WorkspaceTabs.h"

#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include "editor/Splitter.h"

namespace shoal::editor {

namespace {
constexpr int PAGES = 1;  ///< The pages' place in the splitter, between the side panels.
}

WorkspaceTabs::WorkspaceTabs(QWidget* parent)
    : QWidget(parent),
      m_bar(new QTabBar(this)),
      m_pages(new QStackedWidget(this)),
      m_splitter(new Splitter(Qt::Horizontal, this))
{
    m_bar->setObjectName(QStringLiteral("workspaceTabBar"));
    m_bar->setDocumentMode(true);
    m_bar->setExpanding(false);
    m_splitter->setObjectName(QStringLiteral("workspaceSplitter"));
    m_splitter->addWidget(m_pages);
    m_splitter->setCollapsible(0, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins({});
    layout->setSpacing(0);
    layout->addWidget(m_bar);
    layout->addWidget(m_splitter, 1);

    connect(m_bar, &QTabBar::currentChanged, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        emit currentChanged(index);
    });
    connect(m_bar, &QTabBar::tabCloseRequested, this, &WorkspaceTabs::tabCloseRequested);
    // The pages keep the tabs' order.
    connect(m_bar, &QTabBar::tabMoved, this, [this](int from, int to) {
        QWidget* page = m_pages->widget(from);
        m_pages->removeWidget(page);
        m_pages->insertWidget(to, page);
        m_pages->setCurrentIndex(m_bar->currentIndex());
    });
}

void WorkspaceTabs::setSidePanels(QWidget* left, QWidget* right, std::pair<int, int> widths)
{
    m_splitter->insertWidget(0, left);
    m_splitter->addWidget(right);
    m_splitter->setCollapsible(PAGES, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(PAGES, 1);
    m_splitter->setStretchFactor(2, 0);
    const int total = std::max(m_splitter->width(), widths.first + widths.second + 1);
    m_splitter->setSizes({widths.first, total - widths.first - widths.second, widths.second});
}

std::pair<int, int> WorkspaceTabs::sideWidths() const
{
    const QList<int> sizes = m_splitter->sizes();
    return sizes.size() == 3 ? std::pair{sizes[0], sizes[2]} : std::pair{0, 0};
}

int WorkspaceTabs::addTab(QWidget* page, const QString& text)
{
    m_pages->addWidget(page);
    return m_bar->addTab(text);
}

void WorkspaceTabs::removeTab(int index)
{
    if (QWidget* page = widget(index)) {
        m_pages->removeWidget(page);  // first, so the tab bar's current matches a page
        m_bar->removeTab(index);
    }
}

int WorkspaceTabs::count() const
{
    return m_bar->count();
}

QWidget* WorkspaceTabs::widget(int index) const
{
    return m_pages->widget(index);
}

int WorkspaceTabs::indexOf(const QWidget* page) const
{
    return m_pages->indexOf(page);
}

int WorkspaceTabs::currentIndex() const
{
    return m_bar->currentIndex();
}

QWidget* WorkspaceTabs::currentWidget() const
{
    return widget(currentIndex());
}

void WorkspaceTabs::setCurrentIndex(int index)
{
    m_bar->setCurrentIndex(index);
}

void WorkspaceTabs::setCurrentWidget(QWidget* page)
{
    setCurrentIndex(indexOf(page));
}

QString WorkspaceTabs::tabText(int index) const
{
    return m_bar->tabText(index);
}

void WorkspaceTabs::setTabText(int index, const QString& text)
{
    m_bar->setTabText(index, text);
}

}  // namespace shoal::editor
