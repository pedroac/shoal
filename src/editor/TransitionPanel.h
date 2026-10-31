#pragma once

#include <QMap>
#include <QPointer>
#include <QWidget>
#include <optional>

#include "core/TransitionTypes.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace shoal::editor {

class FlowScene;

/// The flow's selected transition, all of it: its id; its mode and where it goes; its
/// trigger, the event the engine fires and the conditions it requires; its effect, a
/// transition type with its duration and easing and a field for each option the type takes,
/// made from the type's file; and what it carries, the state keys and the entities, each
/// onto a mark in the destination. Each edit is one undo step. An effect of a type the
/// project doesn't have is shown by its id, with its duration and easing, and its values
/// are kept.
class TransitionPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionPanel(QWidget* parent = nullptr);

    /// Shows `flow`'s one selected transition; null shows nothing.
    void setFlowScene(FlowScene* flow);
    /// The one selected transition, or empty.
    QString transitionId() const;

    QLabel* summary() const { return m_summary; }
    QLineEdit* idField() const { return m_idField; }
    /// Its items' data are the modes' names; push_modifier is disabled when the scene has
    /// no modifier to run.
    QComboBox* modeBox() const { return m_modeBox; }
    /// The destination, for replace and push; its items' data are the scenes' ids.
    QComboBox* toBox() const { return m_toBox; }
    QComboBox* underlyingBox() const { return m_underlyingBox; }
    /// The modifier a push_modifier runs, one of the scene's.
    QComboBox* modifierBox() const { return m_modifierBox; }
    QLineEdit* eventField() const { return m_eventField; }
    /// The conditions, separated by commas.
    QLineEdit* requiresField() const { return m_requiresField; }
    /// The flow's state keys, checked when the transition carries them; a key it carries
    /// that the flow doesn't declare is listed too.
    QListWidget* stateList() const { return m_stateList; }
    /// A key to declare in the flow, and carry: Return declares it.
    QLineEdit* newStateField() const { return m_newStateField; }
    /// The mark a carried entity lands on, or null if the transition doesn't carry it.
    QComboBox* markBox(const QString& entityId) const { return m_markBoxes.value(entityId); }
    /// An entity of the scene to carry, the mark it lands on, and the button carrying it.
    QComboBox* carryEntityBox() const { return m_carryEntityBox; }
    QComboBox* carryMarkBox() const { return m_carryMarkBox; }
    QToolButton* carryButton() const { return m_carryButton; }
    /// Why the last edit couldn't be applied, or empty.
    QString message() const;
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// The selected transition, or null.
    std::optional<core::Transition> transition() const;
    void refresh();
    void refreshTransition(const core::Transition* transition);
    void refreshCarried(const core::Transition* transition);
    /// Replaces the selected transition with `changed`, as `text`; false if it can't be.
    bool apply(const core::Transition& changed, const QString& text);
    void applyId();
    void chooseMode(int index);
    void applyTrigger();
    void applyState(QListWidgetItem* item);
    void declareState();
    void showMessage(const QString& message);
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
    QLineEdit* m_idField;
    QComboBox* m_modeBox;
    QComboBox* m_toBox;
    QComboBox* m_underlyingBox;
    QComboBox* m_modifierBox;
    QFormLayout* m_route;  ///< The id, the mode and where it goes.
    QLineEdit* m_eventField;
    QLineEdit* m_requiresField;
    QListWidget* m_stateList;
    QLineEdit* m_newStateField;
    QGridLayout* m_carried;  ///< A row for each carried entity.
    QMap<QString, QComboBox*> m_markBoxes;
    QString m_carriedFor;  ///< What the rows are for, so they are only remade when it changes.
    QComboBox* m_carryEntityBox;
    QComboBox* m_carryMarkBox;
    QToolButton* m_carryButton;
    QWidget* m_carriedEntities;  ///< The rows and the carry row, for replace and push.
    QLabel* m_message;
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
