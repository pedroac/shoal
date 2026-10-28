#pragma once

#include <QDialog>

#include "core/ProjectTemplates.h"

class QComboBox;
class QLineEdit;

namespace shoal::editor {

/// Asks for a new project's name and template. Where it lives is asked on its first save.
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    QString projectName() const;
    core::ProjectTemplate projectTemplate() const;

private:
    QLineEdit* m_name;
    QComboBox* m_template;
};

}  // namespace shoal::editor
