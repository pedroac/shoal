#pragma once

#include <QString>

class QDoubleSpinBox;
class QLabel;
class QWidget;

namespace shoal::editor {

/// The largest size a panel accepts, in pixels.
constexpr double MAX_SIZE = 100000;

/// A number box committed on Return, on leaving it and on each step, not on each keystroke.
QDoubleSpinBox* numberBox(QWidget* parent, const QString& name, double range, int decimals,
                          double step);
/// Shows `value` unless the box is being typed in.
void showValue(QDoubleSpinBox* box, double value);

/// A hidden label, with the error icon, for why an edit couldn't be applied.
QLabel* messageLabel(QWidget* parent, const QString& name);
/// Shows `message` in a label from messageLabel(); empty hides it.
void showMessage(QLabel* label, const QString& message);

}  // namespace shoal::editor
