#include "editor/NewProjectDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

namespace shoal::editor {

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent), m_name(new QLineEdit(this)), m_template(new QComboBox(this))
{
    setWindowTitle(tr("New Project"));

    m_name->setText(tr("Untitled"));
    m_name->selectAll();
    m_template->addItem(tr("Default (1920×1080, expand)"),
                        QVariant::fromValue(int(core::ProjectTemplate::Default)));
    m_template->addItem(tr("Pixel Art (640×360, letterbox, integer scale)"),
                        QVariant::fromValue(int(core::ProjectTemplate::PixelArt)));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_name, &QLineEdit::textChanged, this, [buttons](const QString& text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });

    auto* layout = new QFormLayout(this);
    layout->addRow(tr("&Name:"), m_name);
    layout->addRow(tr("&Template:"), m_template);
    layout->addRow(buttons);
}

QString NewProjectDialog::projectName() const
{
    return m_name->text().trimmed();
}

core::ProjectTemplate NewProjectDialog::projectTemplate() const
{
    return core::ProjectTemplate(m_template->currentData().toInt());
}

}  // namespace shoal::editor
