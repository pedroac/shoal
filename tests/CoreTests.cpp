#include <QFile>
#include <QJsonArray>
#include <QTest>

#include "core/JsonReader.h"
#include "core/ProjectCodec.h"
#include "core/TransitionTypes.h"
#include "core/Version.h"

using namespace shoal::core;

namespace {

TransitionTypeDefinition builtIn(const QString& id)
{
    const TransitionTypeRead read = readTransitionType(id, builtInTransitionTypes().value(id));
    Q_ASSERT(read.definition);
    return *read.definition;
}

TransitionTypeRead readType(const QByteArray& json)
{
    return readTransitionType(QStringLiteral("t"), TransitionType{parseJsonObject(json)});
}

}  // namespace

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void versionMatchesVersionFile()
    {
        QCOMPARE(version(), QStringLiteral(SHOAL_EXPECTED_VERSION));
    }

    void shipsItsTransitionTypesAsFiles()
    {
        const QMap<QString, TransitionType>& types = builtInTransitionTypes();
        QCOMPARE(types.keys(),
                 (QStringList{"camera_push", "circle_wipe", "cross_dissolve", "dissolve", "dither",
                              "fade", "fade_in", "fade_out", "flashbang", "glitch", "linear_wipe",
                              "pattern_mask", "pixelate", "screen_scroll", "shatter", "slide"}));
        for (auto it = types.cbegin(); it != types.cend(); ++it) {
            // Each reads, and its file is as Shoal writes it.
            const TransitionTypeRead read = readTransitionType(it.key(), *it);
            QVERIFY2(read.definition, qPrintable(it.key() + u": " + read.error));
            QVERIFY2(!read.definition->description.isEmpty(), qPrintable(it.key()));
            Project project;
            project.transitionTypes.insert(it.key(), *it);
            QFile file(builtInTransitionTypesDir() + u'/' + it.key() + QStringLiteral(".json"));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(
                QString::fromUtf8(serializeProject(project).at(paths::transitionType(it.key()))),
                QString::fromUtf8(file.readAll()));
        }
        const TransitionTypeDefinition fade = builtIn(QStringLiteral("fade"));
        QCOMPARE(fade.name, QStringLiteral("Fade"));
        QCOMPARE(fade.options.size(), size_t{2});
        QCOMPARE(fade.options.at(0).type, OptionType::Color);
        QCOMPARE(fade.options.at(1).type, OptionType::Time);
        QCOMPARE(builtIn(QStringLiteral("slide")).option(u"direction")->choices,
                 (QStringList{"left", "right", "up", "down"}));
        QCOMPARE(builtIn(QStringLiteral("pattern_mask")).option(u"mask")->type, OptionType::Path);
    }

    void projectTypesWinOverBuiltInOnes()
    {
        Project project;
        project.transitionTypes.insert(
            QStringLiteral("fade"),
            TransitionType{{{QStringLiteral("name"), QStringLiteral("Ours")}}});
        project.transitionTypes.insert(QStringLiteral("ripple"), TransitionType{});
        const QMap<QString, TransitionType> types = availableTransitionTypes(project);
        QCOMPARE(types.value(QStringLiteral("fade")), project.transitionTypes.value("fade"));
        QVERIFY(types.contains(QStringLiteral("ripple")));
        QVERIFY(types.contains(QStringLiteral("slide")));
        QVERIFY(!isBuiltInTransitionType(project, QStringLiteral("fade")));
        QVERIFY(isBuiltInTransitionType(project, QStringLiteral("slide")));
        // A type with nothing in it is named after its id.
        QCOMPARE(readTransitionType(QStringLiteral("ripple"), TransitionType{}).definition->name,
                 QStringLiteral("ripple"));
    }

    void readsATypesOptions_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");
        QTest::newRow("all kinds")
            << QByteArray(R"({"options": [{"name": "t", "type": "time", "default": 100},)"
                          R"({"name": "n", "type": "number", "min": -1, "max": 1, "default": 0.5},)"
                          R"({"name": "s", "type": "string", "choices": ["a", "b"], "default": "b"},)"
                          R"({"name": "p", "type": "path"},)"
                          R"({"name": "c", "type": "color", "default": "\u0023123456"}]})")
            << QString();
        QTest::newRow("no type")
            << QByteArray(R"({"options": [{"name": "x"}]})")
            << "options[0].type: missing (one of \"time\", \"number\", \"string\", \"path\" or "
               "\"color\")";
        QTest::newRow("unknown type")
            << QByteArray(R"({"options": [{"name": "x", "type": "angle"}]})")
            << R"(options[0].type: expected "time", "number", "string", "path" or "color", found "angle")";
        QTest::newRow("bad name")
            << QByteArray(R"({"options": [{"name": "two words", "type": "path"}]})")
            << "options[0].name: \"two words\" is not a valid id (use letters, digits, _ and -, "
               "starting with a letter or _)";
        QTest::newRow("same name")
            << QByteArray(
                   R"({"options": [{"name": "x", "type": "path"}, {"name": "x", "type": "time"}]})")
            << R"(options[1].name: another option is already named "x")";
        QTest::newRow("default of another type")
            << QByteArray(R"({"options": [{"name": "x", "type": "color", "default": "red"}]})")
            << "options[0].default: \"red\" isn't a value this color option takes";
        QTest::newRow("default out of range")
            << QByteArray(
                   R"({"options": [{"name": "x", "type": "number", "max": 1, "default": 2}]})")
            << "options[0].default: 2 isn't a value this number option takes";
        QTest::newRow("fractional time")
            << QByteArray(R"({"options": [{"name": "x", "type": "time", "default": 1.5}]})")
            << "options[0].default: 1.5 isn't a value this time option takes";
        QTest::newRow("range of a string")
            << QByteArray(R"({"options": [{"name": "x", "type": "string", "min": 0}]})")
            << R"(options[0].min: only a time or a number has a range)";
        QTest::newRow("reversed range")
            << QByteArray(R"({"options": [{"name": "x", "type": "number", "min": 2, "max": 1}]})")
            << R"(options[0].max: must not be below min)";
        QTest::newRow("choices of a number")
            << QByteArray(R"({"options": [{"name": "x", "type": "number", "choices": ["1"]}]})")
            << R"(options[0].choices: only a string has choices)";
        QTest::newRow("whole times")
            << QByteArray(R"({"options": [{"name": "x", "type": "time", "integer": true}]})")
            << R"(options[0].integer: only a number can be limited to whole numbers; a time always is)";
    }

    void readsATypesOptions()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        const TransitionTypeRead read = readType(json);
        QCOMPARE(read.error, error);
        QCOMPARE(read.definition.has_value(), error.isEmpty());
    }

    void newEffectsTakeTheirTypesDefaults()
    {
        const Effect fade = makeEffect(QStringLiteral("fade"), builtIn(QStringLiteral("fade")));
        QCOMPARE(fade.type, QStringLiteral("fade"));
        QCOMPARE(fade.durationMs, DEFAULT_EFFECT_MS);
        QVERIFY(fade.easing.isEmpty());
        QCOMPARE(fade.params, (QJsonObject{{QStringLiteral("color"), QStringLiteral("#000000")},
                                           {QStringLiteral("hold"), 0}}));
        // An option without a default is left out until it is set.
        QCOMPARE(makeEffect(QStringLiteral("pattern_mask"), builtIn(QStringLiteral("pattern_mask")))
                     .params,
                 (QJsonObject{{QStringLiteral("softness"), 0}}));
    }

    void changingTypeKeepsWhatBothTake()
    {
        const TransitionTypeDefinition fade = builtIn(QStringLiteral("fade"));
        Effect effect = makeEffect(QStringLiteral("fade"), fade);
        effect.durationMs = 800;
        effect.easing = QStringLiteral("ease_out");
        effect.params.insert(QStringLiteral("color"), QStringLiteral("#ff0000"));
        effect.params.insert(QStringLiteral("seed"), 3);  // no option of either type

        const Effect fadeIn = withEffectType(effect, QStringLiteral("fade_in"),
                                             builtIn(QStringLiteral("fade_in")), &fade);
        QCOMPARE(fadeIn.type, QStringLiteral("fade_in"));
        QCOMPARE(fadeIn.durationMs, 800);
        QCOMPARE(fadeIn.easing, QStringLiteral("ease_out"));
        QCOMPARE(fadeIn.params, (QJsonObject{{QStringLiteral("color"), QStringLiteral("#ff0000")},
                                             {QStringLiteral("seed"), 3}}));

        const Effect wipe = withEffectType(effect, QStringLiteral("linear_wipe"),
                                           builtIn(QStringLiteral("linear_wipe")), &fade);
        QCOMPARE(wipe.params, (QJsonObject{{QStringLiteral("direction"), QStringLiteral("left")},
                                           {QStringLiteral("softness"), 0},
                                           {QStringLiteral("seed"), 3}}));
    }

    void readsValuesOrTheirDefaults()
    {
        const TransitionTypeDefinition slide = builtIn(QStringLiteral("slide"));
        const TransitionOption& direction = *slide.option(u"direction");
        Effect effect;
        effect.type = QStringLiteral("slide");
        QCOMPARE(optionValue(effect, direction), QJsonValue(QStringLiteral("left")));
        effect.params.insert(QStringLiteral("direction"), QStringLiteral("up"));
        QCOMPARE(optionValue(effect, direction), QJsonValue(QStringLiteral("up")));
        // One the option doesn't take reads as the default.
        effect.params.insert(QStringLiteral("direction"), QStringLiteral("north"));
        QCOMPARE(optionValue(effect, direction), QJsonValue(QStringLiteral("left")));
        QVERIFY(optionValue(effect, *builtIn(QStringLiteral("pattern_mask")).option(u"mask"))
                    .isUndefined());
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
