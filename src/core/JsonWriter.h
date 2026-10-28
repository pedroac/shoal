#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <vector>

namespace shoal::core {

/// Writes JSON in Shoal's canonical form, so the same data always gives the same bytes:
///
/// - Members in the order they are written; objects one member per line.
/// - Two-space indent, `\n` line endings, a trailing newline, UTF-8 without escaping
///   non-ASCII text.
/// - Arrays of scalars on one line (`[320, 640]`), other arrays one element per line.
/// - Empty containers as `{}` and `[]`.
/// - Numbers in their shortest round-trip form, with `-0` written as `0`.
///
/// QJsonValue objects (data Shoal keeps as written) are written with their keys sorted.
class JsonWriter {
public:
    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    /// Starts a member; the next value written is its value.
    void key(QStringView name);

    void string(QStringView text);
    void number(double value);
    void integer(qint64 value);
    void boolean(bool value);
    void null();
    void value(const QJsonValue& value);

    void stringArray(const QStringList& items);
    void numberArray(std::initializer_list<double> items);
    void booleanArray(std::initializer_list<bool> items);

    /// The document written so far, with a trailing newline.
    QByteArray finish() const;

private:
    struct Level {
        bool isObject = false;
        bool isInline = false;
        bool empty = true;
    };

    void beginValue();
    void openArray(bool isInline);
    void writeIndent(qsizetype depth);

    QByteArray m_out;
    std::vector<Level> m_levels;
    bool m_afterKey = false;
};

/// Quotes and escapes `text` as a JSON string.
QByteArray jsonQuote(QStringView text);

/// Formats `value` as Shoal writes numbers.
QByteArray jsonNumber(double value);

}  // namespace shoal::core
