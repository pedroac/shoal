#include "core/JsonReader.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QRegularExpression>
#include <cmath>
#include <limits>

#include "core/JsonWriter.h"

namespace shoal::core {

namespace {

constexpr qsizetype MAX_QUOTED_LENGTH = 40;

/// The 1-based line and column (in code points) of a byte offset into UTF-8 text.
std::pair<int, int> lineAndColumn(const QByteArray& bytes, qsizetype offset)
{
    int line = 1;
    int column = 1;
    const qsizetype end = std::min(offset, bytes.size());
    for (qsizetype i = 0; i < end; ++i) {
        const auto byte = static_cast<unsigned char>(bytes[i]);
        if (byte == '\n') {
            ++line;
            column = 1;
        }
        else if ((byte & 0xC0) != 0x80) {  // not a UTF-8 continuation byte
            ++column;
        }
    }
    return {line, column};
}

}  // namespace

QJsonObject parseJsonObject(const QByteArray& bytes)
{
    static const QByteArray BOM = QByteArrayLiteral("\xEF\xBB\xBF");
    const qsizetype skipped = bytes.startsWith(BOM) ? BOM.size() : 0;
    const QByteArray text = bytes.mid(skipped);

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError) {
        const auto [line, column] = lineAndColumn(text, error.offset);
        throw FormatError{error.errorString(), line, column};
    }
    if (!document.isObject()) {
        throw FormatError{QStringLiteral("expected a JSON object at the top level")};
    }
    return document.object();
}

QString describeJson(const QJsonValue& value)
{
    switch (value.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            return QStringLiteral("null");
        case QJsonValue::Bool:
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        case QJsonValue::Double:
            return QString::number(value.toDouble(), 'g', QLocale::FloatingPointShortest);
        case QJsonValue::String: {
            QString text = value.toString();
            if (text.size() > MAX_QUOTED_LENGTH) {
                text = text.left(MAX_QUOTED_LENGTH) + QStringLiteral("...");
            }
            return QStringLiteral("\"%1\"").arg(text);
        }
        case QJsonValue::Array:
            return QStringLiteral("an array");
        case QJsonValue::Object:
            return QStringLiteral("an object");
    }
    return {};
}

ObjectReader::ObjectReader(QJsonObject object, QString path)
    : m_object(std::move(object)), m_path(std::move(path))
{
}

bool ObjectReader::has(const QString& key) const
{
    return m_object.contains(key);
}

QString ObjectReader::pathOf(const QString& key) const
{
    return m_path.isEmpty() ? key : m_path + u'.' + key;
}

QJsonValue ObjectReader::take(const QString& key)
{
    m_consumed.insert(key);
    return m_object.value(key);
}

void ObjectReader::fail(const QString& key, const QString& expected, const QJsonValue& found) const
{
    failMessage(key, QStringLiteral("expected %1, found %2").arg(expected, describeJson(found)));
}

void ObjectReader::failMessage(const QString& key, const QString& message) const
{
    throw FormatError{QStringLiteral("%1: %2").arg(pathOf(key), message)};
}

QJsonValue ObjectReader::value(const QString& key)
{
    return take(key);
}

QJsonValue ObjectReader::peek(const QString& key) const
{
    return m_object.value(key);
}

QString ObjectReader::string(const QString& key)
{
    if (!has(key)) {
        failMessage(key, QStringLiteral("missing"));
    }
    const QJsonValue value = take(key);
    if (!value.isString()) {
        fail(key, QStringLiteral("a string"), value);
    }
    return value.toString();
}

QString ObjectReader::optionalString(const QString& key)
{
    if (!has(key)) {
        return {};
    }
    return string(key);
}

QString ObjectReader::id(const QString& key)
{
    const QString text = string(key);
    if (!isValidId(text)) {
        failMessage(key, QStringLiteral("%1 is not a valid id (use letters, digits, _ and -, "
                                        "starting with a letter or _)")
                             .arg(describeJson(text)));
    }
    return text;
}

QString ObjectReader::optionalId(const QString& key)
{
    return has(key) ? id(key) : QString();
}

bool ObjectReader::boolean(const QString& key, bool fallback)
{
    if (!has(key)) {
        return fallback;
    }
    const QJsonValue value = take(key);
    if (!value.isBool()) {
        fail(key, QStringLiteral("true or false"), value);
    }
    return value.toBool();
}

double ObjectReader::number(const QString& key, double fallback)
{
    if (!has(key)) {
        return fallback;
    }
    const QJsonValue value = take(key);
    if (!value.isDouble()) {
        fail(key, QStringLiteral("a number"), value);
    }
    return value.toDouble();
}

int ObjectReader::integer(const QString& key, int fallback, int min, int max)
{
    if (!has(key)) {
        return fallback;
    }
    const QJsonValue value = take(key);
    const double number = value.toDouble();
    if (!value.isDouble() || number != std::floor(number)) {
        fail(key, QStringLiteral("an integer"), value);
    }
    if (number < min || number > max) {
        fail(key, QStringLiteral("an integer from %1 to %2").arg(min).arg(max), value);
    }
    return static_cast<int>(number);
}

QStringList ObjectReader::stringList(const QString& key)
{
    if (!has(key)) {
        return {};
    }
    const QJsonValue value = take(key);
    QStringList items;
    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (!item.isString()) {
                fail(key, QStringLiteral("an array of strings"), value);
            }
            items << item.toString();
        }
        return items;
    }
    fail(key, QStringLiteral("an array of strings"), value);
}

Point ObjectReader::point(const QString& key, Point fallback)
{
    if (!has(key)) {
        return fallback;
    }
    const QJsonValue value = take(key);
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 2 || !array[0].isDouble() || !array[1].isDouble()) {
        fail(key, QStringLiteral("[x, y]"), value);
    }
    return {array[0].toDouble(), array[1].toDouble()};
}

Cell ObjectReader::cell(const QString& key, Cell fallback)
{
    if (!has(key)) {
        return fallback;
    }
    const QJsonValue value = take(key);
    const QJsonArray array = value.toArray();
    constexpr double MAX_INDEX = 1 << 24;  // sprat's coordinate limit
    const auto isIndex = [&](const QJsonValue& index) {
        const double number = index.toDouble();
        return index.isDouble() && number == std::floor(number) && number >= 0 &&
               number <= MAX_INDEX;
    };
    if (!value.isArray() || array.size() != 2 || !isIndex(array[0]) || !isIndex(array[1])) {
        fail(key, QStringLiteral("[column, row], whole numbers from 0 to 16777216"), value);
    }
    return {array[0].toInt(), array[1].toInt()};
}

std::optional<Point> ObjectReader::optionalPoint(const QString& key)
{
    if (!has(key)) {
        return std::nullopt;
    }
    return point(key, {});
}

std::optional<Extent> ObjectReader::optionalExtent(const QString& key)
{
    if (!has(key)) {
        return std::nullopt;
    }
    const QJsonValue value = take(key);
    const QJsonArray array = value.toArray();
    std::optional<Length> sides[2];
    if (value.isArray() && array.size() == 2) {
        for (int i = 0; i < 2; ++i) {
            sides[i] = parseLength(array[i]);
            if (sides[i] && sides[i]->value < 0) {
                sides[i].reset();
            }
        }
    }
    if (!sides[0] || !sides[1]) {
        fail(key,
             QStringLiteral("[width, height], each 0 or more pixels, a percentage such as "
                            "\"50%\" or a number of tiles such as \"2□\""),
             value);
    }
    return Extent{*sides[0], *sides[1]};
}

Anchor ObjectReader::anchor(const QString& key)
{
    if (!has(key)) {
        return {};
    }
    const QJsonValue value = take(key);
    const QJsonArray array = value.toArray();
    constexpr double MAX_INDEX = 1 << 24;  // sprat's coordinate limit
    const auto isAxis = [&](const std::optional<Length>& length) {
        if (!length) {
            return false;
        }
        switch (length->unit) {
            case Unit::Px:
                return length->value == 0;
            case Unit::Percent:
                return true;
            case Unit::Cell:
                return length->value == std::floor(length->value) && length->value >= 0 &&
                       length->value <= MAX_INDEX;
        }
        return false;
    };
    std::optional<Length> axes[2];
    if (value.isArray() && array.size() == 2) {
        axes[0] = parseLength(array[0]);
        axes[1] = parseLength(array[1]);
    }
    if (!isAxis(axes[0]) || !isAxis(axes[1])) {
        fail(key,
             QStringLiteral("[x, y], each 0, a percentage such as \"50%\" or a tile such as "
                            "\"3□\", a whole number from 0"),
             value);
    }
    return Anchor{*axes[0], *axes[1]};
}

std::optional<Color> ObjectReader::optionalColor(const QString& key)
{
    if (!has(key)) {
        return std::nullopt;
    }
    const QJsonValue value = take(key);
    const std::optional<Color> color =
        value.isString() ? Color::fromName(value.toString()) : std::nullopt;
    if (!color) {
        fail(key, QStringLiteral("a color such as \"#e0a030\" or \"#e0a03080\""), value);
    }
    return color;
}

Color ObjectReader::color(const QString& key, Color fallback)
{
    return optionalColor(key).value_or(fallback);
}

Size ObjectReader::size(const QString& key)
{
    if (!has(key)) {
        failMessage(key, QStringLiteral("missing"));
    }
    const QJsonValue value = take(key);
    const QJsonArray array = value.toArray();
    constexpr double MAX_SIDE = 1 << 24;  // sprat's coordinate limit
    const auto isSide = [&](const QJsonValue& side) {
        const double number = side.toDouble();
        return side.isDouble() && number == std::floor(number) && number >= 1 && number <= MAX_SIDE;
    };
    if (!value.isArray() || array.size() != 2 || !isSide(array[0]) || !isSide(array[1])) {
        fail(key, QStringLiteral("[width, height], whole numbers from 1 to 16777216"), value);
    }
    return {array[0].toInt(), array[1].toInt()};
}

QJsonObject ObjectReader::object(const QString& key)
{
    if (!has(key)) {
        return {};
    }
    const QJsonValue value = take(key);
    if (!value.isObject()) {
        fail(key, QStringLiteral("an object"), value);
    }
    return value.toObject();
}

QJsonArray ObjectReader::array(const QString& key)
{
    if (!has(key)) {
        return {};
    }
    const QJsonValue value = take(key);
    if (!value.isArray()) {
        fail(key, QStringLiteral("an array"), value);
    }
    return value.toArray();
}

ObjectReader ObjectReader::child(const QString& key)
{
    return ObjectReader(object(key), pathOf(key));
}

std::vector<ObjectReader> ObjectReader::objectArray(const QString& key)
{
    const QJsonArray items = array(key);
    std::vector<ObjectReader> readers;
    readers.reserve(static_cast<size_t>(items.size()));
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString itemKey = QStringLiteral("%1[%2]").arg(key).arg(i);
        if (!items[i].isObject()) {
            fail(itemKey, QStringLiteral("an object"), items[i]);
        }
        readers.emplace_back(items[i].toObject(), pathOf(itemKey));
    }
    return readers;
}

std::vector<std::pair<QString, ObjectReader>> ObjectReader::objectMap(const QString& key)
{
    const QJsonObject members = object(key);
    std::vector<std::pair<QString, ObjectReader>> readers;
    for (auto it = members.begin(); it != members.end(); ++it) {
        const QString memberKey = QStringLiteral("%1.%2").arg(key, it.key());
        if (!isValidId(it.key())) {
            failMessage(memberKey,
                        QStringLiteral("%1 is not a valid id").arg(describeJson(it.key())));
        }
        if (!it.value().isObject()) {
            fail(memberKey, QStringLiteral("an object"), it.value());
        }
        readers.emplace_back(it.key(), ObjectReader(it.value().toObject(), pathOf(memberKey)));
    }
    return readers;
}

QMap<QString, QString> ObjectReader::stringMap(const QString& key)
{
    const QJsonObject members = object(key);
    QMap<QString, QString> map;
    for (auto it = members.begin(); it != members.end(); ++it) {
        if (!it.value().isString()) {
            fail(QStringLiteral("%1.%2").arg(key, it.key()), QStringLiteral("a string"),
                 it.value());
        }
        map.insert(it.key(), it.value().toString());
    }
    return map;
}

QJsonObject ObjectReader::extras() const
{
    QJsonObject rest;
    for (auto it = m_object.begin(); it != m_object.end(); ++it) {
        if (!m_consumed.contains(it.key())) {
            rest.insert(it.key(), it.value());
        }
    }
    return rest;
}

std::optional<Length> parseLength(const QJsonValue& value)
{
    if (value.isDouble()) {
        return Length::px(value.toDouble());
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    static const QRegularExpression LENGTH(
        QStringLiteral("^(-?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)(%|□)$"));
    const QRegularExpressionMatch match = LENGTH.match(value.toString());
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return Length{number, match.captured(2) == u"%" ? Unit::Percent : Unit::Cell};
}

QJsonValue lengthToJson(const Length& length)
{
    if (length.unit == Unit::Px) {
        return length.value;
    }
    const QString number = QString::fromLatin1(jsonNumber(length.value));
    return number + (length.unit == Unit::Percent ? QChar(u'%') : QChar(0x25A1));
}

int readHeader(ObjectReader& reader, const QString& format, int current)
{
    const QString found = reader.string(QStringLiteral("format"));
    if (found != format) {
        reader.fail(QStringLiteral("format"), QStringLiteral("\"%1\"").arg(format), found);
    }
    const QString versionKey = QStringLiteral("version");
    if (!reader.has(versionKey)) {
        reader.failMessage(versionKey, QStringLiteral("missing"));
    }
    const int version = reader.integer(versionKey, 0, 0, std::numeric_limits<int>::max());
    if (version > current) {
        reader.failMessage(versionKey,
                           QStringLiteral("%1 is newer than this Shoal reads (%2); update Shoal "
                                          "to open this project")
                               .arg(version)
                               .arg(current));
    }
    if (version < 1) {
        reader.fail(versionKey, QString::number(current), reader.value(versionKey));
    }
    return version;
}

}  // namespace shoal::core
