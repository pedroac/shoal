#pragma once

#include <QPointer>
#include <QWidget>
#include <optional>

class QListWidget;
class QListWidgetItem;
class QMimeData;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

/// The presets the project can create entities from: Shoal's own and the project's, which
/// win over a built-in one with the same id. Drag one onto a scene, or double-click it to
/// place it in the middle of the view. A preset that can't be read says why.
class PresetsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PresetsPanel(QWidget* parent = nullptr);

    /// Shows `document`'s presets; null shows nothing.
    void setDocument(core::ProjectDocument* document);
    QListWidget* list() const;
    /// The row of a preset, or null.
    QListWidgetItem* presetRow(const QString& presetId) const;

    /// The MIME type of a drag from this panel: the preset's id, in UTF-8.
    static constexpr const char* MIME_TYPE = "application/x-shoal-preset";
    static QMimeData* mimeData(const QString& presetId);
    /// The preset a drag carries, or nothing.
    static std::optional<QString> presetFromMimeData(const QMimeData* data);

signals:
    /// A preset was double-clicked or activated with the keyboard.
    void placeRequested(const QString& presetId);

private:
    class List;

    void rebuild();

    QPointer<core::ProjectDocument> m_document;
    List* m_list;
};

}  // namespace shoal::editor
