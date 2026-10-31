#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

#include "core/Model.h"

namespace shoal::core {

/// A file that can't be read into the model. Line and column (1-based, columns in code
/// points) are set for syntax errors and 0 otherwise; the message then names the value at
/// fault by its path in the file, such as `entities[2].transform.pos`.
struct FormatError {
    QString message;
    int line = 0;
    int column = 0;
};

/// Parses `bytes` as a JSON object (a UTF-8 byte order mark is skipped).
/// @throws FormatError
QJsonObject parseJsonObject(const QByteArray& bytes);

/// Describes a JSON value for an error message: `"text"`, `12`, `true`, `null`, `an array`,
/// `an object`.
QString describeJson(const QJsonValue& value);

/// Reads the members of one JSON object, naming each value by its path in error messages.
/// Every member read is marked as consumed, and extras() returns the rest, so unmodeled
/// keys survive a save. Every reader throws FormatError.
class ObjectReader {
public:
    ObjectReader(QJsonObject object, QString path);

    bool has(const QString& key) const;
    QString pathOf(const QString& key) const;

    QJsonValue value(const QString& key);
    /// The member, without marking it consumed, so it stays among the extras.
    QJsonValue peek(const QString& key) const;
    QString string(const QString& key);
    QString optionalString(const QString& key);
    bool boolean(const QString& key, bool fallback);
    double number(const QString& key, double fallback);
    int integer(const QString& key, int fallback, int min, int max);
    QStringList stringList(const QString& key);
    Point point(const QString& key, Point fallback);
    std::optional<Point> optionalPoint(const QString& key);
    /// [column, row], whole numbers 0 or more.
    Cell cell(const QString& key, Cell fallback);
    Size size(const QString& key);
    /// An entity's size: [width, height], each 0 or more pixels, a percentage such as
    /// "50%" or a number of tiles such as "2□" (see parseLength()); null when absent.
    std::optional<Extent> optionalExtent(const QString& key);
    /// Where a position is measured from: [x, y], each 0 (the parent's origin), a
    /// percentage such as "50%" or a tile such as "3□", a whole number from 0. Absent, the
    /// parent's origin on both axes.
    Anchor anchor(const QString& key);
    /// `#rrggbb` or `#rrggbbaa`.
    std::optional<Color> optionalColor(const QString& key);
    Color color(const QString& key, Color fallback);
    QJsonObject object(const QString& key);
    QJsonArray array(const QString& key);
    ObjectReader child(const QString& key);
    /// An array of objects, absent meaning empty.
    std::vector<ObjectReader> objectArray(const QString& key);
    /// An object whose values are objects, absent meaning empty; keys must be ids.
    std::vector<std::pair<QString, ObjectReader>> objectMap(const QString& key);
    /// An object whose values are strings, absent meaning empty.
    QMap<QString, QString> stringMap(const QString& key);

    /// An id: required, and matching isValidId().
    QString id(const QString& key);
    /// An id, or empty when the member is absent.
    QString optionalId(const QString& key);

    template <typename E>
    E enumValue(const QString& key, E fallback);

    template <typename E>
    std::optional<E> optionalEnum(const QString& key);

    /// The members not read so far.
    QJsonObject extras() const;

    [[noreturn]] void fail(const QString& key, const QString& expected,
                           const QJsonValue& found) const;
    [[noreturn]] void failMessage(const QString& key, const QString& message) const;

private:
    QJsonValue take(const QString& key);
    template <typename E>
    E parseEnum(const QString& key, const QJsonValue& value) const;

    QJsonObject m_object;
    QString m_path;
    QSet<QString> m_consumed;
};

/// "a or b" / "a, b or c": the names of an enum for an error message.
template <typename E>
QString enumChoices()
{
    QStringList names;
    for (const auto& entry : enumNames(E{})) {
        names << QStringLiteral("\"%1\"").arg(QLatin1StringView(entry.name));
    }
    const QString last = names.takeLast();
    return names.isEmpty() ? last
                           : names.join(QStringLiteral(", ")) + QStringLiteral(" or ") + last;
}

template <typename E>
E ObjectReader::parseEnum(const QString& key, const QJsonValue& value) const
{
    if (value.isString()) {
        if (auto parsed = enumFromName<E>(value.toString())) {
            return *parsed;
        }
    }
    fail(key, enumChoices<E>(), value);
}

template <typename E>
E ObjectReader::enumValue(const QString& key, E fallback)
{
    if (!has(key)) {
        m_consumed.insert(key);
        return fallback;
    }
    return parseEnum<E>(key, take(key));
}

template <typename E>
std::optional<E> ObjectReader::optionalEnum(const QString& key)
{
    if (!has(key)) {
        return std::nullopt;
    }
    return parseEnum<E>(key, take(key));
}

/// A length: a number of pixels, or a string of a number and its unit, "50%" or "3□".
/// Null for anything else.
std::optional<Length> parseLength(const QJsonValue& value);

/// Writes a length as parseLength() reads it.
QJsonValue lengthToJson(const Length& length);

/// Reads `format` and `version`, refusing other formats, and versions newer than `current`
/// or older than 1. Returns the version, for the caller to migrate an older one.
int readHeader(ObjectReader& reader, const QString& format, int current = FORMAT_VERSION);

}  // namespace shoal::core
