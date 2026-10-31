#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <optional>

#include "core/SceneTree.h"

class QCheckBox;
class QComboBox;
class QCompleter;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QStringListModel;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class AssetField;
class ColorButton;
class FlowScene;
class PointGrid;
class SceneCanvas;
class PresetSettingsPanel;
class SceneSettingsPanel;

/// The selected entity of the current scene, or, with none selected, its top level: the
/// scene itself, with its settings (see SceneSettingsPanel), the rect its top-level
/// entities measure against; or, for a preset, the preset's (see PresetSettingsPanel).
///
/// An entity shows its id, its classes, whether it is active and
/// visible, its position (with an offset, when it is relative), angle, scale and flips, then
/// its parts: its size and pivot, its image (an AssetField) and its mode, fill and tint, whether it
/// clips, its container, and its space and parallax. Each edit is one undo step.
///
/// Each axis of the position and of the size has a unit: pixels, a percentage of the
/// parent's rect, or the parent's tiles, when it is a tiled container (see core::Unit).
/// Changing a unit keeps the entity where it is drawn, at the size it has.
///
/// The id must be unique in the scene, and renaming it renames it in the transitions too;
/// classes complete from the class registry and show their declarations' docs. Size and
/// pivot are automatic for an image until set; changing the pivot keeps the entity where it
/// is drawn, and so does making it a tiled container or a free one, for its children. An
/// entity with a size of its own shows its scale only when it isn't 1, since a nine-slice's
/// borders keep their size only when it is resized.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    /// Shows `canvas`'s selection; null shows nothing.
    void setCanvas(SceneCanvas* canvas);
    SceneCanvas* canvas() const { return m_canvas; }
    /// What edits the top level's settings, as the flow's Scene panel does.
    void setFlowScene(FlowScene* flow);
    SceneSettingsPanel* topLevel() const { return m_topLevel; }
    /// A preset's top level: the preset's settings.
    PresetSettingsPanel* presetTopLevel() const { return m_presetTopLevel; }
    /// Whether it shows the top level, nothing being selected, in place of an entity: the
    /// scene's settings, or the preset's.
    bool showsTopLevel() const;

    QLabel* summary() const { return m_summary; }
    QLineEdit* idField() const { return m_idField; }
    QLineEdit* classesField() const { return m_classesField; }
    QCheckBox* activeBox() const { return m_activeBox; }
    QCheckBox* visibleBox() const { return m_visibleBox; }
    /// The position on each axis, in its unit: pixels from the parent's origin, or where
    /// its offset is measured from.
    QDoubleSpinBox* xBox() const { return m_xBox; }
    QDoubleSpinBox* yBox() const { return m_yBox; }
    QComboBox* xUnitBox() const { return m_xUnitBox; }
    QComboBox* yUnitBox() const { return m_yUnitBox; }
    /// The offset in pixels, on an axis that isn't in pixels.
    QDoubleSpinBox* offsetXBox() const { return m_offsetXBox; }
    QDoubleSpinBox* offsetYBox() const { return m_offsetYBox; }
    QDoubleSpinBox* angleBox() const { return m_angleBox; }
    QDoubleSpinBox* scaleXBox() const { return m_scaleXBox; }
    QDoubleSpinBox* scaleYBox() const { return m_scaleYBox; }
    QCheckBox* flipXBox() const { return m_flipXBox; }
    QCheckBox* flipYBox() const { return m_flipYBox; }
    /// "Own size" for an image, "Rect" for an entity without one.
    QCheckBox* sizeBox() const { return m_sizeBox; }
    QDoubleSpinBox* widthBox() const { return m_widthBox; }
    QDoubleSpinBox* heightBox() const { return m_heightBox; }
    QComboBox* widthUnitBox() const { return m_widthUnitBox; }
    QComboBox* heightUnitBox() const { return m_heightUnitBox; }
    QCheckBox* pivotBox() const { return m_pivotBox; }
    PointGrid* pivotGrid() const { return m_pivotGrid; }
    QDoubleSpinBox* pivotXBox() const { return m_pivotXBox; }
    QDoubleSpinBox* pivotYBox() const { return m_pivotYBox; }
    /// The entity's image, chosen by dropping, typing or browsing; clearing it removes it.
    AssetField* imageField() const { return m_imageField; }
    QComboBox* imageModeBox() const { return m_imageModeBox; }
    QCheckBox* fillBox() const { return m_fillBox; }
    ColorButton* fillButton() const { return m_fillButton; }
    ColorButton* tintButton() const { return m_tintButton; }
    QComboBox* tintModeBox() const { return m_tintModeBox; }
    QCheckBox* clipBox() const { return m_clipBox; }
    QComboBox* containerBox() const { return m_containerBox; }
    QDoubleSpinBox* tileWidthBox() const { return m_tileWidthBox; }
    QDoubleSpinBox* tileHeightBox() const { return m_tileHeightBox; }
    PointGrid* tilePivotGrid() const { return m_tilePivotGrid; }
    QDoubleSpinBox* tilePivotXBox() const { return m_tilePivotXBox; }
    QDoubleSpinBox* tilePivotYBox() const { return m_tilePivotYBox; }
    QComboBox* spaceBox() const { return m_spaceBox; }
    QCheckBox* parallaxBox() const { return m_parallaxBox; }
    QDoubleSpinBox* parallaxXBox() const { return m_parallaxXBox; }
    QDoubleSpinBox* parallaxYBox() const { return m_parallaxYBox; }
    /// Whether the scale, the size and the image's mode rows are shown.
    bool showsScale() const;
    bool showsSize() const;
    bool showsImage() const;
    /// Whether the offset row is shown: when an axis of the position isn't in pixels.
    bool showsOffset() const;
    /// Whether the tile pivot row is shown: for a tiled container.
    bool showsTilePivot() const;
    QCompleter* classCompleter() const { return m_completer; }
    /// Why the last edit couldn't be applied, or empty.
    QString message() const;

    /// Classes as typed: names separated by spaces or commas, each with an optional
    /// leading dot, as selectors write them; duplicates are dropped. Null, with `error`
    /// set, when one isn't a valid id.
    static std::optional<QStringList> parseClasses(const QString& text, QString* error);

public slots:
    /// Applies the id field to the entity; false, with a message, if it can't be its id.
    bool applyId();
    /// Applies the classes field; false, with a message, if a class isn't a valid id.
    bool applyClasses();
    /// Applies the position boxes, each in its axis's unit.
    void applyPosition();
    /// Applies the offset boxes, on the axes that aren't in pixels.
    void applyOffset();
    /// Measures the position on `axis` (0 for x, 1 for y) in `unit`, keeping the entity
    /// where it is drawn.
    void setPositionUnit(int axis, core::Unit unit);
    /// Measures the size on `axis` in `unit`, keeping the size it has; nothing for an
    /// automatic size.
    void setSizeUnit(int axis, core::Unit unit);
    /// Applies the angle box, in degrees.
    void applyAngle();
    /// Applies the scale boxes; false, with a message, if one is zero.
    bool applyScale();
    /// Applies the size boxes, each in its side's unit, to an entity with a size of its own.
    void applySize();
    /// Gives the entity a size of its own, keeping the one it has, or makes it automatic
    /// again (for an image) or takes its rect away (for anything else).
    void setOwnSize(bool own);
    /// Sets the entity's pivot (null for automatic), keeping its rect where it is drawn.
    void applyPivot(std::optional<core::Point> pivot);
    void setImageMode(core::ImageMode mode);
    void removeImage();
    /// Gives the entity `image`, as the image field chooses it, or none (see removeImage()). Its
    /// mode stays for another sprite; a size of its own stays.
    void setImage(const std::optional<core::Image>& image);
    void setFill(std::optional<core::Color> fill);
    void setTint(const core::Color& tint);
    void setTintMode(core::TintMode mode);
    void setClip(bool clip);
    /// Makes the entity a container of `type`, or a free one (null), keeping its children
    /// where they are drawn: on the tiles nearest to them in a tiled container. A tiled
    /// container is given a rect if it has none, for its tiles.
    void setContainer(std::optional<core::ContainerType> type);
    /// Applies the tile size boxes; the children keep their cells.
    void applyTileSize();
    /// Sets the point of each tile the children land on; they keep their cells.
    void applyTilePivot(core::Point pivot);
    /// Sets the entity's space; null inherits its parent's.
    void setSpace(std::optional<core::Space> space);
    /// Sets the entity's parallax; null inherits its parent's.
    void setParallax(std::optional<core::Point> parallax);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void refreshClasses();
    void showMessage(const QString& message);
    void setActive(bool active);
    void setVisibleFlag(bool visible);
    void setFlipX(bool flip);
    void setFlipY(bool flip);
    /// The one selected entity, or null.
    const core::Entity* selected(core::EntityPath* path = nullptr) const;
    void push(const core::EntityPath& path, core::Entity entity, const QString& text);
    /// What the parent of the entity at `path` offers it to measure against.
    core::Frame frameOf(const core::EntityPath& path) const;
    void completeClass(const QString& text);
    void insertClass(const QString& name);
    /// The section's form holding the row of `field`.
    QFormLayout* formOf(const QWidget* field) const;
    void setRowVisible(QWidget* field, bool visible);

    QPointer<SceneCanvas> m_canvas;
    QPointer<core::ProjectDocument> m_document;
    QLabel* m_summary;
    QLineEdit* m_idField;
    QLineEdit* m_classesField;
    QCheckBox* m_activeBox;
    QCheckBox* m_visibleBox;
    QDoubleSpinBox* m_xBox;
    QDoubleSpinBox* m_yBox;
    QComboBox* m_xUnitBox;
    QComboBox* m_yUnitBox;
    QDoubleSpinBox* m_offsetXBox;
    QDoubleSpinBox* m_offsetYBox;
    QDoubleSpinBox* m_angleBox;
    QDoubleSpinBox* m_scaleXBox;
    QDoubleSpinBox* m_scaleYBox;
    QDoubleSpinBox* m_widthBox;
    QDoubleSpinBox* m_heightBox;
    QComboBox* m_widthUnitBox;
    QComboBox* m_heightUnitBox;
    QCheckBox* m_flipXBox;
    QCheckBox* m_flipYBox;
    QCheckBox* m_sizeBox;
    QCheckBox* m_pivotBox;
    PointGrid* m_pivotGrid;
    QDoubleSpinBox* m_pivotXBox;
    QDoubleSpinBox* m_pivotYBox;
    AssetField* m_imageField;
    QComboBox* m_imageModeBox;
    QCheckBox* m_fillBox;
    ColorButton* m_fillButton;
    ColorButton* m_tintButton;
    QComboBox* m_tintModeBox;
    QCheckBox* m_clipBox;
    QComboBox* m_containerBox;
    QDoubleSpinBox* m_tileWidthBox;
    QDoubleSpinBox* m_tileHeightBox;
    PointGrid* m_tilePivotGrid;
    QDoubleSpinBox* m_tilePivotXBox;
    QDoubleSpinBox* m_tilePivotYBox;
    QComboBox* m_spaceBox;
    QCheckBox* m_parallaxBox;
    QDoubleSpinBox* m_parallaxXBox;
    QDoubleSpinBox* m_parallaxYBox;
    QLabel* m_message;
    QWidget* m_entityForm;
    SceneSettingsPanel* m_topLevel;
    PresetSettingsPanel* m_presetTopLevel;
    /// The entity's form or the top level's, as big as the bigger, so nothing jumps.
    QStackedWidget* m_pages;
    QWidget* m_scaleRow;
    QWidget* m_sizeRow;
    QWidget* m_imageRow;
    QWidget* m_offsetRow;
    QWidget* m_tilePivotRow;
    QCompleter* m_completer;
    QStringListModel* m_classNames;
};

}  // namespace shoal::editor
