#pragma once

#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "core/Model.h"

namespace shoal::core {

/// An option a transition type takes: a value the engine reads from the effect's `params`,
/// under the option's name.
struct TransitionOption {
    QString name;  ///< An id: its key in `params`.
    OptionType type = OptionType::Number;
    QString description;  ///< A tooltip.
    QJsonValue fallback;  ///< `default` in the file: what it is when unset; undefined for none.
    std::optional<double> minimum;  ///< `min`, for a time or a number.
    std::optional<double> maximum;  ///< `max`, for a time or a number.
    bool integer = false;           ///< For a number: whole numbers only. A time always is.
    QStringList choices;            ///< For a string: the values it can take, if limited.
};

/// What a transition type's file says, once read. Types stay as written in the model (see
/// TransitionType) and are read when they are used, so one the editor can't read never
/// stops a project from opening.
struct TransitionTypeDefinition {
    QString name;         ///< What the editor shows; the type's id when the file has none.
    QString description;  ///< A tooltip, and the panel's text under the type.
    QString preview;  ///< A GIF showing the transition, relative to the type's folder, or empty.
    std::vector<TransitionOption> options;

    const TransitionOption* option(QStringView optionName) const;
};

struct TransitionTypeRead {
    std::optional<TransitionTypeDefinition> definition;  ///< Null when it can't be read.
    QString error;  ///< Names the value at fault by its path, as file errors do.
};

/// Reads a type's `name`, `description`, `preview` and `options`.
TransitionTypeRead readTransitionType(const QString& typeId, const TransitionType& type);
/// Whether `value` is one `option` takes.
bool fitsOption(const TransitionOption& option, const QJsonValue& value);

/// Shoal's own transition types, by id, from the files it ships (assets/transitions).
const QMap<QString, TransitionType>& builtInTransitionTypes();
/// The folder holding the built-in types' files, and their previews.
QString builtInTransitionTypesDir();
/// The types a project's transitions can have: the built-in ones and the project's own,
/// which win over a built-in one with the same id.
QMap<QString, TransitionType> availableTransitionTypes(const Project& project);
/// Whether a type is one of Shoal's own: built in, and not overridden by the project.
bool isBuiltInTransitionType(const Project& project, const QString& typeId);

/// Whether `id` can name a new project type: a valid id no type, built-in or not, has.
/// `ignoring` is the type being renamed, if any.
bool isTransitionTypeIdAvailable(const Project& project, QStringView id, QStringView ignoring = {});
/// `base` if it can name a new type, otherwise `base_2`, `base_3`, ...
QString uniqueTransitionTypeId(const Project& project, const QString& base);

/// How long a new effect lasts.
inline constexpr int DEFAULT_EFFECT_MS = 500;

/// A new effect of the type `typeId`, DEFAULT_EFFECT_MS long and linear, with every option
/// that has a default set to it.
Effect makeEffect(const QString& typeId, const TransitionTypeDefinition& type);
/// `effect` turned into one of the type `typeId`, keeping its duration and easing. A value
/// stays when the new type has an option of its name it fits; values of the old type's
/// other options go, and the new type's options get their defaults. Keys neither type has
/// an option for stay too.
Effect withEffectType(Effect effect, const QString& typeId, const TransitionTypeDefinition& type,
                      const TransitionTypeDefinition* old);
/// The value of `option` in `effect`, or its default; undefined when neither fits it.
QJsonValue optionValue(const Effect& effect, const TransitionOption& option);

}  // namespace shoal::core
