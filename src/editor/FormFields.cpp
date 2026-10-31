#include "editor/FormFields.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>

#include "editor/Icons.h"

namespace shoal::editor {

namespace {

/// A label with the error icon before its text, in the margin it keeps for it, so its
/// text() is the message alone.
class MessageLabel : public QLabel {
public:
    explicit MessageLabel(QWidget* parent) : QLabel(parent)
    {
        const int size = iconSize();
        setContentsMargins(size + size / 3, 0, 0, 0);
        setMinimumHeight(size);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QLabel::paintEvent(event);
        QPainter painter(this);
        const int size = iconSize();
        // Beside the first line.
        const int top = std::max(0, (fontMetrics().height() - size) / 2);
        icon(Icon::Error).paint(&painter, QRect(0, top, size, size));
    }

private:
    int iconSize() const { return fontMetrics().height(); }
};

}  // namespace

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
    auto* label = new MessageLabel(parent);
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
