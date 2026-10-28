#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <optional>

#include "core/SceneTree.h"

class QCheckBox;
class QCompleter;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QStringListModel;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class SceneCanvas;

/// The selected entity of the current scene: its id, its classes, whether it is active
/// and visible, and its angle, scale and flips. Each edit is one undo step. A nine-slice
/// shows its size instead of its scale, which would stretch its borders, unless it has
/// one. The id must be unique
/// in the scene, and renaming it renames it in the transitions too; classes complete from the class
/// registry and show their declarations' docs.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    /// Shows `canvas`'s selection; null shows nothing.
    void setCanvas(SceneCanvas* canvas);
    SceneCanvas* canvas() const { return m_canvas; }

    QLabel* summary() const { return m_summary; }
    QLineEdit* idField() const { return m_idField; }
    QLineEdit* classesField() const { return m_classesField; }
    QCheckBox* activeBox() const { return m_activeBox; }
    QCheckBox* visibleBox() const { return m_visibleBox; }
    QDoubleSpinBox* angleBox() const { return m_angleBox; }
    QDoubleSpinBox* scaleXBox() const { return m_scaleXBox; }
    QDoubleSpinBox* scaleYBox() const { return m_scaleYBox; }
    QDoubleSpinBox* widthBox() const { return m_widthBox; }
    QDoubleSpinBox* heightBox() const { return m_heightBox; }
    /// Whether the scale and the size rows are shown.
    bool showsScale() const;
    bool showsSize() const;
    QCheckBox* flipXBox() const { return m_flipXBox; }
    QCheckBox* flipYBox() const { return m_flipYBox; }
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
    /// Applies the angle box, in degrees.
    void applyAngle();
    /// Applies the scale boxes; false, with a message, if one is zero.
    bool applyScale();
    /// Applies the size boxes to a nine-slice.
    void applySize();

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
    void completeClass(const QString& text);
    void insertClass(const QString& name);

    QPointer<SceneCanvas> m_canvas;
    QPointer<core::ProjectDocument> m_document;
    QLabel* m_summary;
    QLineEdit* m_idField;
    QLineEdit* m_classesField;
    QCheckBox* m_activeBox;
    QCheckBox* m_visibleBox;
    QDoubleSpinBox* m_angleBox;
    QDoubleSpinBox* m_scaleXBox;
    QDoubleSpinBox* m_scaleYBox;
    QDoubleSpinBox* m_widthBox;
    QDoubleSpinBox* m_heightBox;
    QCheckBox* m_flipXBox;
    QCheckBox* m_flipYBox;
    QLabel* m_message;
    QFormLayout* m_form;
    QWidget* m_scaleRow;
    QWidget* m_sizeRow;
    QCompleter* m_completer;
    QStringListModel* m_classNames;
};

}  // namespace shoal::editor
