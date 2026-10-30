#include "editor/FormFields.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>

namespace shoal::editor {

QDoubleSpinBox* numberBox(QWidget* parent, const QString& name, double range, int decimals,
                          double step)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(-range, range);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setKeyboardTracking(false);
    box->setAccelerated(true);
    // Sized for everyday values, not for the range's widest, so panels stay narrow: the
    // box shrinks to that and grows with its row.
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    box->setMinimumWidth(box->fontMetrics().horizontalAdvance(QStringLiteral("x -0000.00")) +
                         box->fontMetrics().height());
    return box;
}

void showValue(QDoubleSpinBox* box, double value)
{
    if (box->hasFocus() && box->cleanText() != box->textFromValue(box->value())) {
        return;
    }
    const QSignalBlocker blocker(box);
    box->setValue(value);
}

QLabel* messageLabel(QWidget* parent, const QString& name)
{
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #c83c28"));
    label->hide();
    return label;
}

void showMessage(QLabel* label, const QString& message)
{
    label->setText(message);
    label->setVisible(!message.isEmpty());
}

}  // namespace shoal::editor
