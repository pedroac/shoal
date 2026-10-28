#pragma once

#include <QColor>
#include <QPointer>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

/// The class registry: every class the scenes use or classes.json declares, with its color,
/// how many entities have it, and its doc. Editing a doc or a color declares the class;
/// each change is one undo step. Classes in use but not declared are shown in italics.
class ClassesPanel : public QWidget {
    Q_OBJECT
public:
    enum Column { NAME, USES, DOC };

    explicit ClassesPanel(QWidget* parent = nullptr);

    /// Shows `document`'s classes; null shows nothing.
    void setDocument(core::ProjectDocument* document);
    QTreeWidget* tree() const;
    /// The row of a class, or null.
    QTreeWidgetItem* classRow(const QString& name) const;

    // --- edits, each one undo step; false if nothing changed ---

    /// Declares `name` with no doc or color; false if it is declared already or not a valid id.
    bool declare(const QString& name);
    bool undeclare(const QString& name);
    bool setDoc(const QString& name, const QString& doc);
    /// An invalid color clears it.
    bool setColor(const QString& name, const QColor& color);

public slots:
    /// Asks for a name and declares it.
    void addClass();

private:
    void rebuild();
    void onItemChanged(QTreeWidgetItem* row, int column);
    void showContextMenu(const QPoint& position);
    void chooseColor(const QString& name);

    QPointer<core::ProjectDocument> m_document;
    QTreeWidget* m_tree;
    QToolButton* m_addButton;
    bool m_rebuilding = false;
};

}  // namespace shoal::editor
