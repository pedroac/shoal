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

/// Draws a sprite as a nine-slice of `size` with its pivot at the middle of a 64x64 image.
QImage drawNineSlice(const Sheet& sheet, const QString& path, QSizeF size, bool flipX, bool flipY)
{
    const Sprite& sprite = *sheet.sprite(path);
    QImage image(64, 64, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (const SpritePlacement& cell : placeNineSlice(sprite, 1, size, flipX, flipY).cells) {
        painter.save();
        painter.translate(32, 32);
        painter.setTransform(cell.transform, true);
        painter.drawImage(cell.target, sheet.atlases[size_t(sprite.atlas)], cell.source);
        painter.restore();
    }
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

    void placesNineSlicesLikeSpratRaylib()
    {
        // A 12x12 panel with 4-pixel borders, untrimmed, its pivot at its top-left.
        Sprite panel;
        panel.rect = QRect(0, 20, 12, 12);
        panel.hasSlice = true;
        panel.slice = QMargins(4, 4, 4, 4);
        panel.contentSize = QSize(12, 12);
        panel.sourceSize = QSize(12, 12);
        QCOMPARE(naturalSize(panel, 0.5), QSizeF(6, 6));

        NineSlicePlacement placed = placeNineSlice(panel, 1, QSizeF(30, 20), false, false);
        QCOMPARE(placed.bounds, QRectF(0, 0, 30, 20));
        QCOMPARE(placed.cells.size(), size_t(9));
        // The corners keep their size; the edges and the middle stretch.
        QCOMPARE(placed.cells[0].bounds, QRectF(0, 0, 4, 4));
        QCOMPARE(placed.cells[0].source, QRectF(0, 20, 4, 4));
        QCOMPARE(placed.cells[1].bounds, QRectF(4, 0, 22, 4));
        QCOMPARE(placed.cells[1].source, QRectF(4, 20, 4, 4));
        QCOMPARE(placed.cells[4].bounds, QRectF(4, 4, 22, 12));
        QCOMPARE(placed.cells[8].bounds, QRectF(26, 16, 4, 4));
        // At half scale, the borders are half as wide in the scene.
        placed = placeNineSlice(panel, 0.5, QSizeF(30, 20), false, false);
        QCOMPARE(placed.cells[0].bounds, QRectF(0, 0, 2, 2));
        // Borders that don't fit shrink proportionally, and the middle goes.
        placed = placeNineSlice(panel, 1, QSizeF(6, 20), false, false);
        QCOMPARE(placed.cells.size(), size_t(6));
        QCOMPARE(placed.cells[0].bounds, QRectF(0, 0, 3, 4));
        // The pivot keeps its fraction of the box, and flips mirror about it.
        panel.pivot = QPoint(6, 12);
        placed = placeNineSlice(panel, 1, QSizeF(30, 20), false, false);
        QCOMPARE(placed.bounds, QRectF(-15, -20, 30, 20));
        QCOMPARE(placed.cells[0].bounds, QRectF(-15, -20, 4, 4));
        placed = placeNineSlice(panel, 1, QSizeF(30, 20), true, true);
        QCOMPARE(placed.bounds, QRectF(-15, 0, 30, 20));
        QCOMPARE(placed.cells[0].bounds, QRectF(11, 16, 4, 4));
        QVERIFY(placeNineSlice(panel, 1, QSizeF(0, 20), false, false).cells.empty());
    }

    void aNineSliceAtItsNaturalSizeDrawsLikeTheSprite()
    {
        const SheetLoad loaded = loadSheet(heroSheet());
        QVERIFY(loaded.sheet);
        // Trimmed and turned when packed, without insets: one cell, the whole content.
        for (const char* name : {"props/door.png", "hero/idle.png", "ui/panel.9.png"}) {
            const QString path = QString::fromLatin1(name);
            const QSizeF size = naturalSize(*loaded.sheet->sprite(path), 1);
            for (const bool flip : {false, true}) {
                QCOMPARE(drawNineSlice(*loaded.sheet, path, size, flip, !flip),
                         draw(*loaded.sheet, path, flip, !flip));
            }
        }
    }

    void cutsATurnedSpriteIntoCells()
    {
        // A 6x4 content of unique colors, trimmed out of a 10x6 image, packed turned.
        QImage upright(6, 4, QImage::Format_ARGB32);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 6; ++x) {
                upright.setPixelColor(x, y, QColor(40 * x, 60 * y, 200));
            }
        }
        for (const int rotation : {0, 90, 270}) {
            Sprite sprite;
            sprite.rotation = rotation;
            sprite.trim = QMargins(1, 2, 3, 0);
            sprite.pivot = QPoint(5, 3);
            sprite.contentSize = QSize(6, 4);
            sprite.sourceSize = QSize(10, 6);
            sprite.hasSlice = true;
            sprite.slice = QMargins(2, 3, 4, 1);
            const QImage atlas = upright.transformed(QTransform().rotate(rotation));
            sprite.rect = QRect(QPoint(), atlas.size());

            const auto render = [&](const std::vector<SpritePlacement>& placements) {
                QImage image(32, 32, QImage::Format_ARGB32);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                for (const SpritePlacement& placement : placements) {
                    painter.save();
                    painter.translate(16, 16);
                    painter.setTransform(placement.transform, true);
                    painter.drawImage(placement.target, atlas, placement.source);
                    painter.restore();
                }
                return image;
            };
            const NineSlicePlacement cells =
                placeNineSlice(sprite, 1, naturalSize(sprite, 1), false, false);
            QVERIFY(cells.cells.size() > 1);
            QCOMPARE(render(cells.cells), render({placeSprite(sprite, 1, false, false)}));
        }
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

    void loadsAStandaloneImageAsASheetOfOneSprite()
    {
        const QString file = support::fixturePath(QStringLiteral("sheets/hero.png"));
        const QImage original(file);
        QVERIFY(!original.isNull());
        const SheetLoad loaded = loadImageAsSheet(file);
        QVERIFY2(loaded.sheet, qPrintable(loaded.error));
        const Sheet& sheet = *loaded.sheet;
        QCOMPARE(sheet.scale, 1.0);
        QCOMPARE(sheet.sprites.size(), size_t(1));
        const Sprite* sprite = sheet.sprite(IMAGE_SPRITE);
        QVERIFY(sprite);
        QCOMPARE(sprite->name, QStringLiteral("hero.png"));
        QCOMPARE(sprite->sourceSize, original.size());
        QCOMPARE(sprite->rect, original.rect());
        QCOMPARE(sprite->pivot, QPoint(0, 0));
        QVERIFY(!sprite->hasSlice);
        QCOMPARE(sheet.atlases.front(), original);
        // Placed with its top-left corner on the origin, at its own size.
        QCOMPARE(placeSprite(*sprite, 1, false, false).bounds, QRectF(original.rect()));

        QTemporaryDir dir;
        QVERIFY(loadImageAsSheet(dir.filePath(QStringLiteral("nope.png"))).error ==
                QStringLiteral("the file doesn't exist"));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("bad.png")), "not an image"));
        QCOMPARE(loadImageAsSheet(dir.filePath(QStringLiteral("bad.png"))).error,
                 QStringLiteral("cannot read the image"));
    }

    void reloadsAnImageWhenItIsWrittenAgain()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("title.png"));
        QImage image(4, 2, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(file));

        SheetCache cache;
        cache.setSheets({}, dir.path(), {{QStringLiteral("title"), QStringLiteral("title.png")}});
        QVERIFY(cache.isImage(QStringLiteral("title")));
        QCOMPARE(cache.sheet(QStringLiteral("title"))->sprites.front().sourceSize, QSize(4, 2));

        QImage bigger(8, 8, QImage::Format_ARGB32);
        bigger.fill(Qt::blue);
        QVERIFY(bigger.save(file));
        QTRY_COMPARE(cache.sheet(QStringLiteral("title"))->sprites.front().sourceSize, QSize(8, 8));

        // Declared as a sheet now: loaded again, as one.
        cache.setSheets({{QStringLiteral("title"), QStringLiteral("title.png")}}, dir.path());
        QVERIFY(!cache.isImage(QStringLiteral("title")));
        QVERIFY(!cache.sheet(QStringLiteral("title")));  // not a .sprat file
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
