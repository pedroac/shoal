#pragma once

#include <QString>
#include <optional>

#include "core/Model.h"

class QMimeData;

namespace shoal::editor {

/// A sprite or an animation of a sprat sheet, as the sheet browser hands it to a canvas:
/// dragged, or activated to place at the middle of the view.
struct SheetItem {
    enum class Kind { Sprite, Animation };

    Kind kind = Kind::Sprite;
    QString sheet;  ///< The sheet's alias.
    QString name;   ///< A sprite's path, or an animation's name.

    /// The MIME type of a drag from the sheet browser.
    static constexpr const char* MIME_TYPE = "application/x-shoal-sheet-item";

    /// A new `sprite` or `animation` entity drawing this item, with no id or layer yet.
    core::Entity toEntity() const;
    QMimeData* toMimeData() const;
    /// The item a drag carries, or nothing if it carries none.
    static std::optional<SheetItem> fromMimeData(const QMimeData* data);

    bool operator==(const SheetItem&) const = default;
};

}  // namespace shoal::editor
