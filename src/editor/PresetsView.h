#pragma once

#include <QWidget>

class QLabel;

namespace shoal::assets {
class FontCache;
class SheetCache;
}  // namespace shoal::assets

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class SceneView;

/// The Presets workspace: the preset current in the Presets list, its entities on a
/// canvas, edited as a scene's are, with the scene tree and the inspector, whose top level
/// is the preset's settings. A built-in preset is shown read-only until it is customized.
class PresetsView : public QWidget {
    Q_OBJECT
public:
    PresetsView(core::ProjectDocument& document, assets::SheetCache& sheets,
                assets::FontCache& fonts, QWidget* parent = nullptr);

    /// The current preset's canvas, or null when there is none.
    SceneView* view() const { return m_view; }

    /// Shows `presetId`'s entities, or, empty, none.
    void showPreset(const QString& presetId);
    void setSnapping(bool snapping);

signals:
    /// view() is another canvas, or none.
    void viewChanged();

private:
    core::ProjectDocument& m_document;
    assets::SheetCache& m_sheets;
    assets::FontCache& m_fonts;
    QLabel* m_placeholder;
    SceneView* m_view = nullptr;
    bool m_snapping = false;
};

}  // namespace shoal::editor
