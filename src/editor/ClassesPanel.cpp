#include "editor/ClassesPanel.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/ClassCommands.h"
#include "core/ProjectDocument.h"

namespace shoal::editor {

namespace {

constexpr int SWATCH_SIZE = 12;

QIcon swatch(const QColor& color, const QColor& outline)
{
    QPixmap pixmap(SWATCH_SIZE, SWATCH_SIZE);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(outline);
    painter.setBrush(color.isValid() ? QBrush(color) : QBrush(Qt::NoBrush));
    painter.drawRect(0, 0, SWATCH_SIZE - 1, SWATCH_SIZE - 1);
    return QIcon(pixmap);
}

}  // namespace

ClassesPanel::ClassesPanel(QWidget* parent)
    : QWidget(parent), m_tree(new QTreeWidget(this)), m_addButton(new QToolButton(this))
{
    m_tree->setObjectName(QStringLiteral("classesTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Class"), tr("Uses"), tr("Doc")});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setSectionResizeMode(NAME, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(USES, QHeaderView::ResizeToContents);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_addButton->setText(QStringLiteral("+"));
    m_addButton->setToolTip(tr("Declare Class"));

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_addButton);
    buttonRow->addStretch();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addLayout(buttonRow);

    connect(m_addButton, &QToolButton::clicked, this, &ClassesPanel::addClass);
    connect(m_tree, &QTreeWidget::itemChanged, this, &ClassesPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* row, int column) {
                if (column == NAME) {
                    chooseColor(row->text(NAME));
                }
            });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ClassesPanel::showContextMenu);
    setDocument(nullptr);
}

void ClassesPanel::setDocument(core::ProjectDocument* document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document) {
        connect(m_document, &core::ProjectDocument::classesChanged, this, &ClassesPanel::rebuild);
        // The uses change with the scenes.
        connect(m_document, &core::ProjectDocument::sceneContentChanged, this,
                &ClassesPanel::rebuild);
        connect(m_document, &core::ProjectDocument::sceneAdded, this, &ClassesPanel::rebuild);
        connect(m_document, &core::ProjectDocument::sceneRemoved, this, &ClassesPanel::rebuild);
    }
    setEnabled(m_document != nullptr);
    rebuild();
}

QTreeWidget* ClassesPanel::tree() const
{
    return m_tree;
}

QTreeWidgetItem* ClassesPanel::classRow(const QString& name) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(NAME) == name) {
            return m_tree->topLevelItem(i);
        }
    }
    return nullptr;
}

bool ClassesPanel::declare(const QString& name)
{
    if (!m_document || !core::isValidId(name)) {
        return false;
    }
    const auto& registry = m_document->project().classes;
    if (registry && registry->classes.contains(name)) {
        return false;
    }
    m_document->commands().push(std::make_unique<core::SetClassDeclarationCommand>(
        *m_document, name, core::ClassDeclaration{}));
    return true;
}

bool ClassesPanel::undeclare(const QString& name)
{
    if (!m_document) {
        return false;
    }
    const auto& registry = m_document->project().classes;
    if (!registry || !registry->classes.contains(name)) {
        return false;
    }
    m_document->commands().push(
        std::make_unique<core::SetClassDeclarationCommand>(*m_document, name, std::nullopt));
    return true;
}

bool ClassesPanel::setDoc(const QString& name, const QString& doc)
{
    if (!m_document || !core::isValidId(name)) {
        return false;
    }
    const auto& registry = m_document->project().classes;
    const bool declared = registry && registry->classes.contains(name);
    core::ClassDeclaration declaration =
        declared ? registry->classes.value(name) : core::ClassDeclaration{};
    if (declared && declaration.doc == doc) {
        return false;
    }
    declaration.doc = doc;
    m_document->commands().push(
        std::make_unique<core::SetClassDeclarationCommand>(*m_document, name, declaration));
    return true;
}

bool ClassesPanel::setColor(const QString& name, const QColor& color)
{
    if (!m_document || !core::isValidId(name)) {
        return false;
    }
    const auto& registry = m_document->project().classes;
    const bool declared = registry && registry->classes.contains(name);
    core::ClassDeclaration declaration =
        declared ? registry->classes.value(name) : core::ClassDeclaration{};
    const QString text = color.isValid() ? color.name(QColor::HexRgb) : QString();
    if (declared && declaration.color == text) {
        return false;
    }
    declaration.color = text;
    m_document->commands().push(
        std::make_unique<core::SetClassDeclarationCommand>(*m_document, name, declaration));
    return true;
}

void ClassesPanel::addClass()
{
    if (!m_document) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Declare Class"), tr("Class name:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (!core::isValidId(name)) {
        QMessageBox::warning(this, tr("Declare Class"),
                             tr("“%1” isn't a valid class: use letters, digits, _ and -, "
                                "starting with a letter or _.")
                                 .arg(name));
        return;
    }
    if (!declare(name)) {
        QMessageBox::information(this, tr("Declare Class"),
                                 tr("“%1” is declared already.").arg(name));
    }
}

void ClassesPanel::rebuild()
{
    m_rebuilding = true;
    const QString current = m_tree->currentItem() ? m_tree->currentItem()->text(NAME) : QString();
    m_tree->clear();
    if (m_document) {
        const QMap<QString, int> uses = m_document->classUses();
        const auto& registry = m_document->project().classes;
        QStringList names = uses.keys();
        if (registry) {
            for (const QString& name : registry->classes.keys()) {
                if (!names.contains(name)) {
                    names << name;
                }
            }
        }
        names.sort();
        const QColor outline = palette().color(QPalette::Text);
        for (const QString& name : std::as_const(names)) {
            const bool declared = registry && registry->classes.contains(name);
            const core::ClassDeclaration declaration =
                declared ? registry->classes.value(name) : core::ClassDeclaration{};
            auto* row = new QTreeWidgetItem(m_tree);
            row->setText(NAME, name);
            row->setIcon(NAME, swatch(QColor(declaration.color), outline));
            row->setText(USES, QString::number(uses.value(name)));
            row->setTextAlignment(USES, Qt::AlignRight | Qt::AlignVCenter);
            row->setText(DOC, declaration.doc);
            row->setToolTip(NAME, declared ? tr("Double-click to choose its color")
                                           : tr("Used, not declared in classes.json"));
            row->setToolTip(DOC, declaration.doc);
            if (!declared) {
                QFont font = row->font(NAME);
                font.setItalic(true);
                row->setFont(NAME, font);
            }
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            if (name == current) {
                m_tree->setCurrentItem(row);
            }
        }
    }
    m_rebuilding = false;
}

void ClassesPanel::onItemChanged(QTreeWidgetItem* row, int column)
{
    if (m_rebuilding) {
        return;
    }
    // Deferred: the command rebuilds the tree, deleting the row the editor is closing on.
    if (column == DOC) {
        QMetaObject::invokeMethod(
            this,
            [this, name = row->text(NAME), doc = row->text(DOC).trimmed()] { setDoc(name, doc); },
            Qt::QueuedConnection);
    }
    else {
        // Only the doc is edited in place; anything else typed is put back.
        QMetaObject::invokeMethod(this, &ClassesPanel::rebuild, Qt::QueuedConnection);
    }
}

void ClassesPanel::showContextMenu(const QPoint& position)
{
    QTreeWidgetItem* row = m_tree->itemAt(position);
    if (!row || !m_document) {
        return;
    }
    const QString name = row->text(NAME);
    const auto& registry = m_document->project().classes;
    const bool declared = registry && registry->classes.contains(name);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(tr("Edit Doc"), this, [this, name] {
        if (QTreeWidgetItem* current = classRow(name)) {
            m_tree->editItem(current, DOC);
        }
    });
    menu->addAction(tr("Choose Color…"), this, [this, name] { chooseColor(name); });
    if (declared && !registry->classes.value(name).color.isEmpty()) {
        menu->addAction(tr("Clear Color"), this, [this, name] { setColor(name, QColor()); });
    }
    menu->addSeparator();
    if (declared) {
        menu->addAction(tr("Undeclare"), this, [this, name] { undeclare(name); });
    }
    else {
        menu->addAction(tr("Declare"), this, [this, name] { declare(name); });
    }
    menu->popup(m_tree->viewport()->mapToGlobal(position));
}

void ClassesPanel::chooseColor(const QString& name)
{
    if (!m_document) {
        return;
    }
    const auto& registry = m_document->project().classes;
    const QColor initial(registry ? registry->classes.value(name).color : QString());
    const QColor color = QColorDialog::getColor(initial.isValid() ? initial : Qt::white, this,
                                                tr("Color of .%1").arg(name));
    if (color.isValid()) {
        setColor(name, color);
    }
}

}  // namespace shoal::editor
