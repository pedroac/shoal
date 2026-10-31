#pragma once

#include <QPointer>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

/// The transition types a project's effects can have: Shoal's own and the project's, which
/// win over a built-in one with the same id, by name. A type that can't be read says so. The
/// current one is the one the transition type panel edits. New, Duplicate and Remove (Revert,
/// for a customized built-in type) act on the list, each one undo step.
class TransitionTypesPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionTypesPanel(QWidget* parent = nullptr);

    /// Shows `document`'s types; null shows nothing.
    void setDocument(core::ProjectDocument* document);
    QListWidget* list() const { return m_list; }
    /// The row of a type, or null.
    QListWidgetItem* typeRow(const QString& typeId) const;
    /// The current row's type, or empty.
    QString currentType() const;
    void setCurrentType(const QString& typeId);
    QToolButton* newButton() const { return m_newButton; }
    QToolButton* duplicateButton() const { return m_duplicateButton; }
    QToolButton* removeButton() const { return m_removeButton; }

public slots:
    /// Adds a project type with no options, and makes it current; returns its id.
    QString newType();
    /// Copies the current type, built-in or not, into a new project type, and makes it
    /// current; returns its id, or empty.
    QString duplicateType();
    /// Removes the current project type; a customized built-in one reverts to Shoal's.
    void removeType();

signals:
    /// The current row changed, to `typeId` or none (empty).
    void currentTypeChanged(const QString& typeId);

private:
    void rebuild();
    void updateButtons();

    QPointer<core::ProjectDocument> m_document;
    QListWidget* m_list;
    QToolButton* m_newButton;
    QToolButton* m_duplicateButton;
    QToolButton* m_removeButton;
};

}  // namespace shoal::editor
