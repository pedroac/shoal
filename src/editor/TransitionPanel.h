#pragma once

#include <QMap>
#include <QPointer>
#include <QWidget>
#include <optional>

#include "core/TransitionTypes.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;

namespace shoal::editor {

class FlowScene;

/// The flow's selected transition: where it goes, and its effect: a transition type, its
/// duration and easing, and a field for each option the type takes, made from the type's
/// file. Each edit is one undo step. An effect of a type the project doesn't have is shown
/// by its id, with its duration and easing, and its values are kept.
class TransitionPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionPanel(QWidget* parent = nullptr);

    /// Shows `flow`'s one selected transition; null shows nothing.
    void setFlowScene(FlowScene* flow);
    /// The one selected transition, or empty.
    QString transitionId() const;

    QLabel* summary() const { return m_summary; }
    /// "None" first, then each type the project has, by name, then the effect's own type if
    /// the project doesn't have it. Its items' data are the types' ids, empty for none.
    QComboBox* typeBox() const { return m_typeBox; }
    /// The type's description.
    QLabel* description() const { return m_description; }
    /// The type's preview GIF, hidden when it has none.
    QLabel* preview() const { return m_preview; }
    QDoubleSpinBox* durationBox() const { return m_durationBox; }
    /// Its items' data are the easings' names, empty for linear.
    QComboBox* easingBox() const { return m_easingBox; }
    /// The field of the type's option `name`, or null.
    QWidget* optionField(const QString& name) const;

private:
    void refresh();
    /// Makes the option fields for `type`, taking away the others.
    void buildOptions(const core::TransitionTypeDefinition* type);
    QWidget* makeField(const core::TransitionOption& option);
    void showOption(const core::TransitionOption& option, const QJsonValue& value);
    /// The selected transition's effect, or null.
    std::optional<core::Effect> effect() const;
    void setEffect(const std::optional<core::Effect>& effect);
    void chooseType(int index);
    void applyTiming();
    /// Sets an option's value; undefined unsets it.
    void setOption(const QString& name, const QJsonValue& value);
    void browsePath(const QString& name);

    QPointer<FlowScene> m_flow;
    QLabel* m_summary;
    QComboBox* m_typeBox;
    QLabel* m_description;
    QLabel* m_preview;
    QDoubleSpinBox* m_durationBox;
    QComboBox* m_easingBox;
    QFormLayout* m_options;
    QMap<QString, QWidget*> m_fields;  ///< The option fields, by option.
    /// The type the option fields are for, as its id and file, so they are only remade when
    /// it changes: a field may be in its own signal's handler.
    QString m_optionsFor;
    std::optional<core::TransitionTypeDefinition> m_optionsType;
    QString m_previewFile;
};

}  // namespace shoal::editor
