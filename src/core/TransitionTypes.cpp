#include "core/TransitionTypes.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

#include "core/JsonReader.h"

namespace shoal::core {

namespace {

const QString BUILT_IN_DIR = QStringLiteral(":/shoal/transitions");

bool isTimeOrNumber(OptionType type)
{
    return type == OptionType::Time || type == OptionType::Number;
}

TransitionOption readOption(ObjectReader& reader)
{
    TransitionOption option;
    option.name = reader.id(QStringLiteral("name"));
    if (!reader.has(QStringLiteral("type"))) {
        reader.failMessage(QStringLiteral("type"),
                           QStringLiteral("missing (one of %1)").arg(enumChoices<OptionType>()));
    }
    option.type = reader.enumValue(QStringLiteral("type"), OptionType::Number);
    option.description = reader.optionalString(QStringLiteral("description"));

    const QString minKey = QStringLiteral("min");
    const QString maxKey = QStringLiteral("max");
    const QString integerKey = QStringLiteral("integer");
    const QString choicesKey = QStringLiteral("choices");
    if (!isTimeOrNumber(option.type)) {
        for (const QString& key : {minKey, maxKey}) {
            if (reader.has(key)) {
                reader.failMessage(key, QStringLiteral("only a time or a number has a range"));
            }
        }
    }
    if (reader.has(minKey)) {
        option.minimum = reader.number(minKey, 0);
    }
    if (reader.has(maxKey)) {
        option.maximum = reader.number(maxKey, 0);
    }
    if (option.minimum && option.maximum && *option.minimum > *option.maximum) {
        reader.failMessage(maxKey, QStringLiteral("must not be below min"));
    }
    if (reader.has(integerKey) && option.type != OptionType::Number) {
        reader.failMessage(integerKey, QStringLiteral("only a number can be limited to whole "
                                                      "numbers; a time always is"));
    }
    option.integer = reader.boolean(integerKey, false);
    if (reader.has(choicesKey) && option.type != OptionType::String) {
        reader.failMessage(choicesKey, QStringLiteral("only a string has choices"));
    }
    option.choices = reader.stringList(choicesKey);

    const QString defaultKey = QStringLiteral("default");
    if (reader.has(defaultKey)) {
        option.fallback = reader.value(defaultKey);
        if (!fitsOption(option, option.fallback)) {
            reader.failMessage(defaultKey,
                               QStringLiteral("%1 isn't a value this %2 option takes")
                                   .arg(describeJson(option.fallback), enumName(option.type)));
        }
    }
    else {
        option.fallback = QJsonValue(QJsonValue::Undefined);
    }
    return option;
}

/// The types in Shoal's resources, by the names of their files.
QMap<QString, TransitionType> readBuiltInTypes()
{
    QMap<QString, TransitionType> types;
    const QDir dir(BUILT_IN_DIR);
    for (const QString& fileName : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        try {
            ObjectReader reader(parseJsonObject(file.readAll()), QString());
            readHeader(reader, QStringLiteral("shoal-transition"), TRANSITION_TYPE_FORMAT_VERSION);
            types.insert(QFileInfo(fileName).completeBaseName(), TransitionType{reader.extras()});
        }
        catch (const FormatError&) {
            Q_ASSERT_X(false, "readBuiltInTypes", qPrintable(fileName));  // tests read them all
        }
    }
    return types;
}

}  // namespace

const TransitionOption* TransitionTypeDefinition::option(QStringView optionName) const
{
    const auto it =
        std::find_if(options.begin(), options.end(),
                     [&](const TransitionOption& option) { return option.name == optionName; });
    return it == options.end() ? nullptr : &*it;
}

TransitionTypeRead readTransitionType(const QString& typeId, const TransitionType& type)
{
    TransitionTypeRead result;
    try {
        ObjectReader reader(type.content, QString());
        TransitionTypeDefinition definition;
        definition.name = reader.optionalString(QStringLiteral("name"));
        if (definition.name.isEmpty()) {
            definition.name = typeId;
        }
        definition.description = reader.optionalString(QStringLiteral("description"));
        definition.preview = reader.optionalString(QStringLiteral("preview"));
        for (ObjectReader& item : reader.objectArray(QStringLiteral("options"))) {
            TransitionOption option = readOption(item);
            if (definition.option(option.name)) {
                item.failMessage(QStringLiteral("name"),
                                 QStringLiteral("another option is already named %1")
                                     .arg(describeJson(option.name)));
            }
            definition.options.push_back(std::move(option));
        }
        result.definition = std::move(definition);
    }
    catch (const FormatError& error) {
        result.error = error.message;
    }
    return result;
}

bool fitsOption(const TransitionOption& option, const QJsonValue& value)
{
    switch (option.type) {
        case OptionType::Time:
        case OptionType::Number: {
            if (!value.isDouble()) {
                return false;
            }
            const double number = value.toDouble();
            const bool whole = option.type == OptionType::Time || option.integer;
            return (!whole || number == std::floor(number)) &&
                   (option.type != OptionType::Time || number >= 0) &&
                   (!option.minimum || number >= *option.minimum) &&
                   (!option.maximum || number <= *option.maximum);
        }
        case OptionType::String:
            return value.isString() &&
                   (option.choices.isEmpty() || option.choices.contains(value.toString()));
        case OptionType::Path:
            return value.isString();
        case OptionType::Color:
            return value.isString() && Color::fromName(value.toString()).has_value();
    }
    return false;
}

const QMap<QString, TransitionType>& builtInTransitionTypes()
{
    static const QMap<QString, TransitionType> types = readBuiltInTypes();
    return types;
}

QString builtInTransitionTypesDir()
{
    return BUILT_IN_DIR;
}

QMap<QString, TransitionType> availableTransitionTypes(const Project& project)
{
    QMap<QString, TransitionType> types = builtInTransitionTypes();
    types.insert(project.transitionTypes);
    return types;
}

bool isBuiltInTransitionType(const Project& project, const QString& typeId)
{
    return !project.transitionTypes.contains(typeId) && builtInTransitionTypes().contains(typeId);
}

Effect makeEffect(const QString& typeId, const TransitionTypeDefinition& type)
{
    Effect effect;
    effect.durationMs = DEFAULT_EFFECT_MS;
    return withEffectType(std::move(effect), typeId, type, nullptr);
}

Effect withEffectType(Effect effect, const QString& typeId, const TransitionTypeDefinition& type,
                      const TransitionTypeDefinition* old)
{
    effect.type = typeId;
    QJsonObject params;
    for (auto it = effect.params.cbegin(); it != effect.params.cend(); ++it) {
        const TransitionOption* option = type.option(it.key());
        if (option ? fitsOption(*option, it.value()) : !(old && old->option(it.key()))) {
            params.insert(it.key(), it.value());
        }
    }
    for (const TransitionOption& option : type.options) {
        if (!params.contains(option.name) && !option.fallback.isUndefined()) {
            params.insert(option.name, option.fallback);
        }
    }
    effect.params = params;
    return effect;
}

QJsonValue optionValue(const Effect& effect, const TransitionOption& option)
{
    const QJsonValue value = effect.params.value(option.name);
    if (fitsOption(option, value)) {
        return value;
    }
    return option.fallback;
}

}  // namespace shoal::core
