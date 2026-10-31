#pragma once

#include <QIcon>
#include <QMessageBox>

class QPainter;
class QRect;

namespace shoal::editor {

/// The editor's own icons, from assets/icons. The plain ones take the palette's colors,
/// so they suit light and dark themes alike; the others keep their own.
enum class Icon {
    Add,         ///< Adds something.
    Remove,      ///< Removes something.
    Filter,      ///< Narrows a list down to what is typed.
    Flow,        ///< The scene flow.
    Scene,       ///< A scene, or the scenes.
    Preset,      ///< Presets.
    Assets,      ///< The project's assets.
    MoveUp,      ///< Moves an item up its ordered list.
    MoveDown,    ///< Moves an item down its ordered list.
    DragHandle,  ///< What can be dragged, such as a splitter's handle; its dots run upright.
    Drop,        ///< Where something can be dropped.
    Error,       ///< An error message.
    Warning,     ///< A warning message.
};

QIcon icon(Icon which);

/// The drag handle's dots across the middle of `rect`, running along `orientation`: upright
/// for Qt::Vertical, turned for Qt::Horizontal.
void paintDragHandle(QPainter& painter, const QRect& rect, Qt::Orientation orientation);

/// Gives `box` the icon for `which`, Icon::Error or Icon::Warning, at the style's size.
void setMessageIcon(QMessageBox& box, Icon which);
/// QMessageBox::warning(), with the editor's warning icon.
QMessageBox::StandardButton warning(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

}  // namespace shoal::editor
