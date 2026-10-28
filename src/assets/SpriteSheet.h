#pragma once

#include <QHash>
#include <QImage>
#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <memory>
#include <vector>

namespace shoal::assets {

/// A sprite copied out of a sprat sheet. Geometry is in atlas pixels, as sprat-lib gives it.
struct Sprite {
    QString name;  ///< The basename; not unique.
    QString path;  ///< Unique in the sheet; what entities refer to.
    QString aliasOf;
    int atlas = 0;
    QRect rect;        ///< In the atlas, as stored: turned when `rotation` isn't 0.
    int rotation = 0;  ///< 0, 90 or 270: the clockwise turn applied when it was packed.
    QMargins trim;     ///< Pixels trimmed off each side of the original image.
    QPoint pivot;      ///< In original-image coordinates.
    bool hasSlice = false;
    QMargins slice;     ///< Nine-slice insets, in original-image coordinates.
    QSize contentSize;  ///< The trimmed image, upright.
    QSize sourceSize;   ///< The original image, trim included.
};

struct Animation {
    QString name;
    int fps = 0;
    std::vector<int> frames;  ///< Indexes into Sheet::sprites.
    bool flipX = false;
    bool flipY = false;
    QString aliasOf;
};

/// A sprat sheet, copied out of sprat-lib (load, convert, free), with its atlas images.
struct Sheet {
    QString file;  ///< The .sprat file it was loaded from.
    double scale = 1;
    std::vector<Sprite> sprites;  ///< In the sheet's order.
    std::vector<Animation> animations;
    std::vector<QImage> atlases;  ///< Null where the image couldn't be read.
    QStringList atlasFiles;       ///< Resolved against the sheet's folder.
    QStringList warnings;         ///< Atlas images that couldn't be read.

    /// The index of the sprite with that path, or -1.
    int spriteIndex(const QString& path) const { return m_byPath.value(path, -1); }
    const Sprite* sprite(const QString& path) const;
    const Animation* animation(const QString& name) const;

    QHash<QString, int> m_byPath;
};

struct SheetLoad {
    std::shared_ptr<const Sheet> sheet;  ///< Null on failure.
    QString error;  ///< sprat-lib's message, with the line and column when it gives them.
};

/// Loads a .sprat file and the atlas images it names, relative to its folder.
SheetLoad loadSheet(const QString& file);

/// How to draw a sprite following sprat-lib's drawing recipe, with its pivot at the origin.
struct SpritePlacement {
    /// Maps `target` into pivot space: draw `source` from the atlas into `target` with
    /// this transform.
    QTransform transform;
    QRectF target;
    QRectF source;
    /// The visible image, upright and flipped, in pivot space: for picking and outlines.
    QRectF bounds;
};

/// Places `sprite` drawn at `drawScale` (1 / sheet scale gives the art's original size),
/// mirrored about its pivot when flipped.
SpritePlacement placeSprite(const Sprite& sprite, double drawScale, bool flipX, bool flipY);

}  // namespace shoal::assets
