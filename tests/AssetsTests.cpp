#include <QPainter>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "Support.h"
#include "assets/SheetCache.h"
#include "assets/SpratLibrary.h"
#include "assets/SpriteSheet.h"

using namespace shoal::assets;

namespace {

const QColor YELLOW(240, 220, 40);
const QColor BROWN(120, 80, 40);

QString heroSheet()
{
    return support::fixturePath(QStringLiteral("sheets/hero.sprat"));
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

/// Draws a sprite with its pivot at the middle of a 64x64 image.
QImage draw(const Sheet& sheet, const QString& path, bool flipX, bool flipY)
{
    const Sprite& sprite = *sheet.sprite(path);
    const SpritePlacement placement = placeSprite(sprite, 1, flipX, flipY);
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(32, 32);
    painter.setTransform(placement.transform, true);
    painter.drawImage(placement.target, sheet.atlases[size_t(sprite.atlas)], placement.source);
    return image;
}

}  // namespace

class AssetsTests : public QObject {
    Q_OBJECT
private slots:
    void linksSpratLib()
    {
        const QString version = spratLibraryVersion();
        QVERIFY2(
            QRegularExpression(QStringLiteral("^v\\d+\\.\\d+\\.\\d+$")).match(version).hasMatch(),
            qPrintable(version));
    }

    void loadsASheetAndItsAtlas()
    {
        const SheetLoad loaded = loadSheet(heroSheet());
        QVERIFY2(loaded.sheet, qPrintable(loaded.error));
        const Sheet& sheet = *loaded.sheet;
        QCOMPARE(sheet.sprites.size(), size_t(4));
        QCOMPARE(sheet.warnings, QStringList());
        QCOMPARE(sheet.atlases.size(), size_t(1));
        QCOMPARE(sheet.atlases[0].size(), QSize(32, 32));

        const Sprite* door = sheet.sprite(QStringLiteral("props/door.png"));
        QVERIFY(door);
        QCOMPARE(door->name, QStringLiteral("door"));
        QCOMPARE(door->rotation, 90);
        QCOMPARE(door->rect, QRect(12, 8, 6, 10));
        QCOMPARE(door->contentSize, QSize(10, 6));
        QCOMPARE(door->sourceSize, QSize(14, 8));
        QCOMPARE(door->pivot, QPoint(7, 8));
        QVERIFY(sheet.sprite(QStringLiteral("ui/panel.9.png"))->hasSlice);
        QVERIFY(!sheet.sprite(QStringLiteral("door")));  // by path, not by name

        const Animation* idle = sheet.animation(QStringLiteral("idle"));
        QVERIFY(idle);
        QCOMPARE(idle->frames, std::vector<int>{0});
    }

    void reportsWhatSpratLibSays()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("broken.sprat"));
        QVERIFY(writeFile(file, "sprat-raw 2\nsheet 1 0 0 0 0 0 0 0 0 0 0 0: 0:\n"));
        const SheetLoad loaded = loadSheet(file);
        QVERIFY(!loaded.sheet);
        QVERIFY2(loaded.error.contains(QStringLiteral("atlas")), qPrintable(loaded.error));
        QVERIFY2(loaded.error.startsWith(QStringLiteral("2:")), qPrintable(loaded.error));
    }

    void warnsAboutAMissingAtlas()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("hero.sprat"));
        QVERIFY(writeFile(file, readFile(heroSheet())));
        const SheetLoad loaded = loadSheet(file);
        QVERIFY(loaded.sheet);
        QCOMPARE(loaded.sheet->warnings,
                 QStringList{QStringLiteral("atlas 0 (hero.png): cannot read the image")});
        QVERIFY(loaded.sheet->atlases[0].isNull());
    }

    void placesSpritesLikeSpratLibsRecipe()
    {
        // The example in sprat-lib's README: a 10x6 image trimmed by 1 left, 2 top and 3
        // right, packed turned 90 degrees, pivot (5, 3): it covers x -4..2, y -1..3.
        Sprite sprite;
        sprite.rect = QRect(0, 0, 4, 6);
        sprite.rotation = 90;
        sprite.trim = QMargins(1, 2, 3, 0);
        sprite.pivot = QPoint(5, 3);
        sprite.contentSize = QSize(6, 4);
        QCOMPARE(placeSprite(sprite, 1, false, false).bounds, QRectF(-4, -1, 6, 4));
        QCOMPARE(placeSprite(sprite, 2, false, false).bounds, QRectF(-8, -2, 12, 8));
        // Flipping mirrors about the pivot.
        QCOMPARE(placeSprite(sprite, 1, true, false).bounds, QRectF(-2, -1, 6, 4));
        QCOMPARE(placeSprite(sprite, 1, false, true).bounds, QRectF(-4, -3, 6, 4));
    }

    void drawsARotatedSpriteUpright()
    {
        const SheetLoad loaded = loadSheet(heroSheet());
        QVERIFY(loaded.sheet);
        // Upright, the door spans x 1-7..11-7 and y 2-8..8-8 around the pivot at (32, 32),
        // and its top row is yellow.
        QImage image = draw(*loaded.sheet, QStringLiteral("props/door.png"), false, false);
        QCOMPARE(image.pixelColor(30, 26), YELLOW);
        QCOMPARE(image.pixelColor(30, 27), BROWN);
        QCOMPARE(image.pixelColor(35, 31), BROWN);
        QCOMPARE(image.pixelColor(36, 31).alpha(), 0);
        QCOMPARE(image.pixelColor(30, 32).alpha(), 0);

        image = draw(*loaded.sheet, QStringLiteral("props/door.png"), false, true);
        QCOMPARE(image.pixelColor(30, 37), YELLOW);  // mirrored below the pivot
        QCOMPARE(image.pixelColor(30, 36), BROWN);
    }

    void reloadsASheetWhenItIsWrittenAgain()
    {
        QTemporaryDir dir;
        const QByteArray original = readFile(heroSheet());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.png")),
                          readFile(support::fixturePath(QStringLiteral("sheets/hero.png")))));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.sprat")), original));

        SheetCache cache;
        QSignalSpy changed(&cache, &SheetCache::sheetChanged);
        cache.setSheets({{QStringLiteral("hero"), QStringLiteral("hero.sprat")}}, dir.path());
        QCOMPARE(changed.count(), 1);
        QVERIFY(cache.sheet(QStringLiteral("hero")));
        QCOMPARE(
            cache.sheet(QStringLiteral("hero"))->sprite(QStringLiteral("props/bush.png"))->pivot,
            QPoint(8, 8));

        // The bush's pivot moves to (8, 0); same length, so the string total still holds.
        QByteArray edited = original;
        edited.replace("0 0 0 0 8 8 0 0 0 0 0 0 0 0 4:bush", "0 0 0 0 8 0 0 0 0 0 0 0 0 0 4:bush");
        QVERIFY(edited != original);
        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.sprat")), edited));
        QTRY_COMPARE(
            cache.sheet(QStringLiteral("hero"))->sprite(QStringLiteral("props/bush.png"))->pivot,
            QPoint(8, 0));

        // A broken write keeps what was loaded and says why.
        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.sprat")), "sprat-raw 2\n"));
        QTRY_VERIFY(!cache.error(QStringLiteral("hero")).isEmpty());
        QVERIFY(cache.sheet(QStringLiteral("hero")));

        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.sprat")), original));
        QTRY_VERIFY(cache.error(QStringLiteral("hero")).isEmpty());
    }

    void loadsASheetOnceItIsWritten()
    {
        QTemporaryDir dir;
        SheetCache cache;
        cache.setSheets({{QStringLiteral("hero"), QStringLiteral("hero.sprat")}}, dir.path());
        QVERIFY(!cache.sheet(QStringLiteral("hero")));
        QVERIFY(!cache.error(QStringLiteral("hero")).isEmpty());

        QVERIFY(writeFile(dir.filePath(QStringLiteral("hero.sprat")), readFile(heroSheet())));
        QTRY_VERIFY(cache.sheet(QStringLiteral("hero")));
    }

    void forgetsSheetsNoLongerDeclared()
    {
        SheetCache cache;
        const QString folder = support::fixturePath(QStringLiteral("sheets"));
        cache.setSheets({{QStringLiteral("hero"), QStringLiteral("hero.sprat")}}, folder);
        QSignalSpy changed(&cache, &SheetCache::sheetChanged);
        cache.setSheets({{QStringLiteral("hero"), QStringLiteral("hero.sprat")}}, folder);
        QCOMPARE(changed.count(), 0);  // unchanged, so not loaded again
        cache.setSheets({}, folder);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!cache.sheet(QStringLiteral("hero")));
    }
};

QTEST_GUILESS_MAIN(AssetsTests)
#include "AssetsTests.moc"
