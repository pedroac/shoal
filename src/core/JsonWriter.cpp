#include "core/JsonWriter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace shoal::core {

namespace {

constexpr int INDENT_WIDTH = 2;

bool isScalar(const QJsonValue& value)
{
    return !value.isArray() && !value.isObject();
}

}  // namespace

QByteArray jsonQuote(QStringView text)
{
    QString escaped;
    escaped.reserve(text.size() + 2);
    escaped += u'"';
    for (QChar c : text) {
        switch (c.unicode()) {
            case u'"':
                escaped += u"\\\"";
                break;
            case u'\\':
                escaped += u"\\\\";
                break;
            case u'\b':
                escaped += u"\\b";
                break;
            case u'\f':
                escaped += u"\\f";
                break;
            case u'\n':
                escaped += u"\\n";
                break;
            case u'\r':
                escaped += u"\\r";
                break;
            case u'\t':
                escaped += u"\\t";
                break;
            default:
                if (c.unicode() < 0x20) {
                    escaped += QStringLiteral("\\u%1").arg(static_cast<uint>(c.unicode()), 4, 16,
                                                           QLatin1Char('0'));
                }
                else {
                    escaped += c;
                }
        }
    }
    escaped += u'"';
    return escaped.toUtf8();
}

QByteArray jsonNumber(double value)
{
    // The model never holds non-finite numbers (JSON can't express them, so the reader
    // never produces them); write null rather than invalid JSON if one slips through.
    if (!std::isfinite(value)) {
        return QByteArrayLiteral("null");
    }
    if (value == 0) {
        return QByteArrayLiteral("0");
    }
    return QString::number(value, 'g', QLocale::FloatingPointShortest).toLatin1();
}

void JsonWriter::writeIndent(qsizetype depth)
{
    m_out += '\n';
    m_out += QByteArray(depth * INDENT_WIDTH, ' ');
}

void JsonWriter::beginValue()
{
    if (m_afterKey) {
        m_afterKey = false;
        return;
    }
    if (m_levels.empty()) {
        return;
    }
    Level& level = m_levels.back();
    Q_ASSERT(!level.isObject);  // object members start with key()
    if (level.isInline) {
        if (!level.empty) {
            m_out += ", ";
        }
    }
    else {
        if (!level.empty) {
            m_out += ',';
        }
        writeIndent(static_cast<qsizetype>(m_levels.size()));
    }
    level.empty = false;
}

void JsonWriter::key(QStringView name)
{
    Q_ASSERT(!m_levels.empty() && m_levels.back().isObject && !m_afterKey);
    Level& level = m_levels.back();
    if (!level.empty) {
        m_out += ',';
    }
    level.empty = false;
    writeIndent(static_cast<qsizetype>(m_levels.size()));
    m_out += jsonQuote(name);
    m_out += ": ";
    m_afterKey = true;
}

void JsonWriter::beginObject()
{
    beginValue();
    m_out += '{';
    m_levels.push_back({true, false, true});
}

void JsonWriter::endObject()
{
    Q_ASSERT(!m_levels.empty() && m_levels.back().isObject && !m_afterKey);
    const bool empty = m_levels.back().empty;
    m_levels.pop_back();
    if (!empty) {
        writeIndent(static_cast<qsizetype>(m_levels.size()));
    }
    m_out += '}';
}

void JsonWriter::openArray(bool isInline)
{
    beginValue();
    m_out += '[';
    m_levels.push_back({false, isInline, true});
}

void JsonWriter::beginArray()
{
    openArray(false);
}

void JsonWriter::endArray()
{
    Q_ASSERT(!m_levels.empty() && !m_levels.back().isObject);
    const Level level = m_levels.back();
    m_levels.pop_back();
    if (!level.isInline && !level.empty) {
        writeIndent(static_cast<qsizetype>(m_levels.size()));
    }
    m_out += ']';
}

void JsonWriter::string(QStringView text)
{
    beginValue();
    m_out += jsonQuote(text);
}

void JsonWriter::number(double value)
{
    beginValue();
    m_out += jsonNumber(value);
}

void JsonWriter::integer(qint64 value)
{
    beginValue();
    m_out += QByteArray::number(value);
}

void JsonWriter::boolean(bool value)
{
    beginValue();
    m_out += value ? "true" : "false";
}

void JsonWriter::null()
{
    beginValue();
    m_out += "null";
}

void JsonWriter::value(const QJsonValue& value)
{
    switch (value.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            null();
            break;
        case QJsonValue::Bool:
            boolean(value.toBool());
            break;
        case QJsonValue::Double:
            number(value.toDouble());
            break;
        case QJsonValue::String:
            string(value.toString());
            break;
        case QJsonValue::Array: {
            const QJsonArray array = value.toArray();
            openArray(std::all_of(array.begin(), array.end(), isScalar));
            for (const QJsonValue& item : array) {
                this->value(item);
            }
            endArray();
            break;
        }
        case QJsonValue::Object: {
            const QJsonObject object = value.toObject();
            beginObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                key(it.key());
                this->value(it.value());
            }
            endObject();
            break;
        }
    }
}

void JsonWriter::stringArray(const QStringList& items)
{
    openArray(true);
    for (const QString& item : items) {
        string(item);
    }
    endArray();
}

void JsonWriter::numberArray(std::initializer_list<double> items)
{
    openArray(true);
    for (double item : items) {
        number(item);
    }
    endArray();
}

void JsonWriter::booleanArray(std::initializer_list<bool> items)
{
    openArray(true);
    for (bool item : items) {
        boolean(item);
    }
    endArray();
}

QByteArray JsonWriter::finish() const
{
    Q_ASSERT(m_levels.empty() && !m_afterKey);
    return m_out + '\n';
}

}  // namespace shoal::core
