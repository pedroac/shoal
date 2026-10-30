#pragma once

#include <QPointer>
#include <QWidget>
#include <optional>

class QListWidget;
class QListWidgetItem;
class QMimeData;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

/// The presets the project can create entities from: Shoal's own and the project's, which
/// win over a built-in one with the same id. Drag one onto a scene, or double-click it to
/// place it in the middle of the view; clicking one makes it current, dragging doesn't. A preset
/// that can't be read says why. It follows the document's presets, keeping the current one, which
/// the Presets tab edits. New, Duplicate and Remove act on the list, each one undo step.
class PresetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PresetsPanel(QWidget* parent = nullptr);

    /// Shows `document`'s presets; null shows nothing.
    void setDocument(core::ProjectDocument* document);
    QListWidget* list() const;
    /// The row of a preset, or null.
    QListWidgetItem* presetRow(const QString& presetId) const;
    /// The current row's preset, or empty.
    QString currentPreset() const;
    void setCurrentPreset(const QString& presetId);
    QToolButton* newButton() const { return m_newButton; }
    QToolButton* duplicateButton() const { return m_duplicateButton; }
    QToolButton* removeButton() const { return m_removeButton; }

    /// The MIME type of a drag from this panel: the preset's id, in UTF-8.
    static constexpr const char* MIME_TYPE = "application/x-shoal-preset";
    static QMimeData* mimeData(const QString& presetId);
    /// The preset a drag carries, or nothing.
    static std::optional<QString> presetFromMimeData(const QMimeData* data);

public slots:
    /// Adds a project preset creating one mark, and makes it current; returns its id.
    QString newPreset();
    /// Copies the current preset, built-in or not, into a new project preset, and makes it
    /// current; returns its id, or empty.
    QString duplicatePreset();
    /// Removes the current project preset; a customized built-in one reverts to Shoal's.
    void removePreset();

signals:
    /// A preset was double-clicked or activated with the keyboard.
    void placeRequested(const QString& presetId);
    /// The current row changed, to `presetId` or none (empty).
    void currentPresetChanged(const QString& presetId);
    /// A preset was added, to be edited.
    void editRequested(const QString& presetId);

private:
    class List;

    void rebuild();
    void updateButtons();

    QPointer<core::ProjectDocument> m_document;
    List* m_list;
    QToolButton* m_newButton;
    QToolButton* m_duplicateButton;
    QToolButton* m_removeButton;
};

}  // namespace shoal::editor
