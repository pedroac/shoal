#include "assets/SpriteSheet.h"

#include <sprat/sprat.h>

#include <QDir>
#include <QFileInfo>

namespace shoal::assets {

namespace {

QString text(const char* utf8)
{
    return utf8 ? QString::fromUtf8(utf8) : QString();
}

/// Frees a sheet when it goes out of scope, whatever path the conversion takes.
struct SheetHandle {
    sprat_sheet* sheet = nullptr;
    ~SheetHandle() { sprat_sheet_free(sheet); }
};

Sprite convertSprite(const sprat_sprite& s)
{
    Sprite sprite;
    sprite.name = text(s.name);
    sprite.path = text(s.path);
    sprite.aliasOf = text(s.alias_of);
    sprite.atlas = int(s.atlas_index);
    sprite.rect = QRect(s.rect.x, s.rect.y, s.rect.w, s.rect.h);
    sprite.rotation = s.rotation;
    sprite.trim = QMargins(s.trim.left, s.trim.top, s.trim.right, s.trim.bottom);
    sprite.pivot = QPoint(s.pivot.x, s.pivot.y);
    sprite.hasSlice = s.has_slice;
    sprite.slice = QMargins(s.slice.left, s.slice.top, s.slice.right, s.slice.bottom);
    const sprat_size content = sprat_sprite_content_size(&s);
    const sprat_size source = sprat_sprite_source_size(&s);
    sprite.contentSize = QSize(content.w, content.h);
    sprite.sourceSize = QSize(source.w, source.h);
    return sprite;
}

}  // namespace

const Sprite* Sheet::sprite(const QString& path) const
{
    const int index = spriteIndex(path);
    return index < 0 ? nullptr : &sprites[size_t(index)];
}

const Animation* Sheet::animation(const QString& name) const
{
    for (const Animation& animation : animations) {
        if (animation.name == name) {
            return &animation;
        }
    }
    return nullptr;
}

SheetLoad loadSheet(const QString& file)
{
    SheetLoad result;
    SheetHandle handle;
    sprat_error error{};
    if (sprat_sheet_load_file(file.toUtf8().constData(), &handle.sheet, &error) != SPRAT_OK) {
        result.error = error.line > 0 ? QStringLiteral("%1:%2: %3")
                                            .arg(error.line)
                                            .arg(error.column)
                                            .arg(QString::fromUtf8(error.message))
                                      : QString::fromUtf8(error.message);
        return result;
    }

    const sprat_sheet& s = *handle.sheet;
    auto sheet = std::make_shared<Sheet>();
    sheet->file = file;
    sheet->scale = s.scale;
    for (size_t i = 0; i < s.sprite_count; ++i) {
        sheet->sprites.push_back(convertSprite(s.sprites[i]));
        sheet->m_byPath.insert(sheet->sprites.back().path, int(i));
    }
    for (size_t i = 0; i < s.animation_count; ++i) {
        const sprat_animation& a = s.animations[i];
        Animation animation;
        animation.name = text(a.name);
        animation.fps = a.fps;
        animation.frames.assign(a.frames, a.frames + a.frame_count);
        animation.flipX = a.flip & SPRAT_FLIP_H;
        animation.flipY = a.flip & SPRAT_FLIP_V;
        animation.aliasOf = text(a.alias_of);
        sheet->animations.push_back(std::move(animation));
    }

    const QDir folder = QFileInfo(file).dir();
    for (size_t i = 0; i < s.atlas_count; ++i) {
        const sprat_atlas& atlas = s.atlases[i];
        const QString path = text(atlas.path);
        const QString atlasFile = QDir::cleanPath(folder.filePath(path));
        QImage image;
        if (!image.load(atlasFile)) {
            sheet->warnings
                << QStringLiteral("atlas %1 (%2): cannot read the image").arg(i).arg(path);
        }
        else if (image.width() != atlas.width || image.height() != atlas.height) {
            sheet->warnings << QStringLiteral(
                                   "atlas %1 (%2): the image is %3x%4, the sheet "
                                   "says %5x%6")
                                   .arg(i)
                                   .arg(path)
                                   .arg(image.width())
                                   .arg(image.height())
                                   .arg(atlas.width)
                                   .arg(atlas.height);
            image = {};  // its rects wouldn't line up
        }
        sheet->atlasFiles << atlasFile;
        sheet->atlases.push_back(std::move(image));
    }
    result.sheet = std::move(sheet);
    return result;
}

SpritePlacement placeSprite(const Sprite& sprite, double drawScale, bool flipX, bool flipY)
{
    // sprat-lib's recipe: the stored region, centered on `center` and turned back by
    // -rotation, puts the upright content's top-left at trim - pivot.
    const QSizeF content(sprite.contentSize);
    const QPointF center(sprite.trim.left() + content.width() / 2 - sprite.pivot.x(),
                         sprite.trim.top() + content.height() / 2 - sprite.pivot.y());
    const QSizeF stored = QSizeF(sprite.rect.size()) * drawScale;

    SpritePlacement placement;
    placement.source = QRectF(sprite.rect);
    placement.target = QRectF(QPointF(-stored.width() / 2, -stored.height() / 2), stored);
    // Points go through the rotation, then the move to the center, then the mirror.
    placement.transform =
        QTransform().rotate(-sprite.rotation) *
        QTransform::fromTranslate(center.x() * drawScale, center.y() * drawScale) *
        QTransform::fromScale(flipX ? -1 : 1, flipY ? -1 : 1);
    placement.bounds = placement.transform.mapRect(placement.target);
    return placement;
}

}  // namespace shoal::assets
