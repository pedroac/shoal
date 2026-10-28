#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "core/JsonWriter.h"

using shoal::core::jsonNumber;
using shoal::core::jsonQuote;
using shoal::core::JsonWriter;

class JsonWriterTests : public QObject {
    Q_OBJECT
private slots:
    void writesCanonicalLayout()
    {
        JsonWriter writer;
        writer.beginObject();
        writer.key(u"name");
        writer.string(u"hero");
        writer.key(u"pos");
        writer.numberArray({320, 640.5});
        writer.key(u"classes");
        writer.stringArray({QStringLiteral("a"), QStringLiteral("b")});
        writer.key(u"children");
        writer.beginArray();
        writer.beginObject();
        writer.key(u"visible");
        writer.boolean(false);
        writer.endObject();
        writer.beginObject();
        writer.endObject();
        writer.endArray();
        writer.key(u"empty_list");
        writer.beginArray();
        writer.endArray();
        writer.key(u"nothing");
        writer.null();
        writer.endObject();

        QCOMPARE(writer.finish(), QByteArray("{\n"
                                             "  \"name\": \"hero\",\n"
                                             "  \"pos\": [320, 640.5],\n"
                                             "  \"classes\": [\"a\", \"b\"],\n"
                                             "  \"children\": [\n"
                                             "    {\n"
                                             "      \"visible\": false\n"
                                             "    },\n"
                                             "    {}\n"
                                             "  ],\n"
                                             "  \"empty_list\": [],\n"
                                             "  \"nothing\": null\n"
                                             "}\n"));
    }

    void writesJsonValuesWithSortedKeys()
    {
        const QJsonObject object{
            {QStringLiteral("zeta"), 1},
            {QStringLiteral("alpha"), QJsonArray{1, 2}},
            {QStringLiteral("mid"), QJsonArray{QJsonObject{{QStringLiteral("k"), true}}}},
        };
        JsonWriter writer;
        writer.value(object);
        QCOMPARE(writer.finish(), QByteArray("{\n"
                                             "  \"alpha\": [1, 2],\n"
                                             "  \"mid\": [\n"
                                             "    {\n"
                                             "      \"k\": true\n"
                                             "    }\n"
                                             "  ],\n"
                                             "  \"zeta\": 1\n"
                                             "}\n"));
    }

    void formatsNumbersInShortestForm_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("integer") << 2000.0 << QByteArray("2000");
        QTest::newRow("fraction") << 0.1 << QByteArray("0.1");
        QTest::newRow("negative") << -12.5 << QByteArray("-12.5");
        QTest::newRow("float noise kept") << 0.1 + 0.2 << QByteArray("0.30000000000000004");
        QTest::newRow("negative zero") << -0.0 << QByteArray("0");
        QTest::newRow("sprat limit") << 16777216.0 << QByteArray("16777216");
        QTest::newRow("tiny") << 1e-5 << QByteArray("1e-05");
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity() << QByteArray("null");
    }

    void formatsNumbersInShortestForm()
    {
        QFETCH(double, value);
        QFETCH(QByteArray, expected);
        QCOMPARE(jsonNumber(value), expected);
    }

    void escapesStrings()
    {
        QCOMPARE(jsonQuote(u"say \"hi\"\\"), QByteArray("\"say \\\"hi\\\"\\\\\""));
        QCOMPARE(jsonQuote(u"a\nb\tc\r"), QByteArray("\"a\\nb\\tc\\r\""));
        QCOMPARE(jsonQuote(QString(QChar(0x1B))), QByteArray("\"\\u001b\""));
        QCOMPARE(jsonQuote(u"Fée 🐟"), QStringLiteral("\"Fée 🐟\"").toUtf8());
    }
};

QTEST_GUILESS_MAIN(JsonWriterTests)
#include "JsonWriterTests.moc"
