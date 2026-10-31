#include "editor/Thumbnails.h"

#include <QPainter>
#include <algorithm>

#include "assets/SheetCache.h"
#include "assets/SpriteSheet.h"
#include "editor/SheetItem.h"

namespace shoal::editor {

QIcon spriteThumbnail(const assets::Sheet& sheet, const assets::Sprite& sprite,
                      const QPixmap& atlas, bool flipX, bool flipY, int size, double pixelRatio)
{
    const assets::SpritePlacement placement =
        assets::placeSprite(sprite, 1.0 / sheet.scale, flipX, flipY);
    const QRectF bounds = placement.bounds;
    if (atlas.isNull() || bounds.isEmpty()) {
        return {};
    }
    const int pixels = int(size * pixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const double factor = std::min(pixels / bounds.width(), pixels / bounds.height());
    painter.translate(pixels / 2.0, pixels / 2.0);
    painter.scale(factor, factor);
    painter.translate(-bounds.center());
    painter.setTransform(placement.transform, true);
    painter.drawPixmap(placement.target, atlas, placement.source);
    painter.end();
    pixmap.setDevicePixelRatio(pixelRatio);
    return QIcon(pixmap);
}

QIcon itemThumbnail(const assets::SheetCache& sheets, const SheetItem& item, int size,
                    double pixelRatio)
{
    const std::shared_ptr<const assets::Sheet> sheet = sheets.sheet(item.sheet);
    if (!sheet || sheet->sprites.empty()) {
        return {};
    }
    const assets::Sprite* sprite = nullptr;
    bool flipX = false;
    bool flipY = false;
    if (item.kind == SheetItem::Kind::Image) {
        sprite = &sheet->sprites.front();
    }
    else if (item.kind == SheetItem::Kind::Sprite) {
        sprite = sheet->sprite(item.name);
    }
    else if (const assets::Animation* animation = sheet->animation(item.name);
             animation && !animation->frames.empty() &&
             size_t(animation->frames.front()) < sheet->sprites.size()) {
        sprite = &sheet->sprites[size_t(animation->frames.front())];
        flipX = animation->flipX;
        flipY = animation->flipY;
    }
    if (!sprite || size_t(sprite->atlas) >= sheet->atlases.size()) {
        return {};
    }
    return spriteThumbnail(*sheet, *sprite,
                           QPixmap::fromImage(sheet->atlases[size_t(sprite->atlas)]), flipX, flipY,
                           size, pixelRatio);
}

}  // namespace shoal::editor
