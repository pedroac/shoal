#pragma once

#include <QIcon>

namespace shoal::assets {
struct Sheet;
struct Sprite;
class SheetCache;
}  // namespace shoal::assets

namespace shoal::editor {

struct SheetItem;

/// `sprite` drawn whole in a square of `size` pixels on screen, upright and flipped as the
/// canvas draws it, scaled to fill the square; pixel art stays sharp. Empty without an atlas.
QIcon spriteThumbnail(const assets::Sheet& sheet, const assets::Sprite& sprite,
                      const QPixmap& atlas, bool flipX, bool flipY, int size, double pixelRatio);

/// What `item` draws, as spriteThumbnail() does, an animation by its first frame; empty if
/// the cache doesn't have it.
QIcon itemThumbnail(const assets::SheetCache& sheets, const SheetItem& item, int size,
                    double pixelRatio);

}  // namespace shoal::editor
