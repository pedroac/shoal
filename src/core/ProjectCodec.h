#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>
#include <map>
#include <optional>

#include "core/Model.h"

namespace shoal::core {

class ObjectReader;

/// The source files of a project, by path relative to the project folder.
using ProjectFiles = std::map<QString, QByteArray>;

/// A problem with one file. `line` and `column` are 1-based, and 0 when the message names
/// the value at fault by its path instead.
struct FileError {
    QString file;
    QString message;
    int line = 0;
    int column = 0;

    /// "file:line:column: message", or "file: message".
    QString toString() const;
    bool operator==(const FileError&) const = default;
};

namespace paths {
inline const QString MANIFEST = QStringLiteral("project.shoal.json");
inline const QString FLOW = QStringLiteral("flow.json");
inline const QString CLASSES = QStringLiteral("classes.json");
QString scene(const QString& sceneId);
QString modifier(const QString& sceneId, const QString& modifierId);
QString preset(const QString& presetId);
}  // namespace paths

/// Whether `path` is one of the files a project consists of (and Shoal writes): the
/// manifest, flow.json, classes.json, presets/*.json, scenes/*/scene.json and
/// scenes/*/modifiers/*.json.
bool isProjectFile(QStringView path);

struct ParseResult {
    std::optional<Project> project;  ///< Set when every file was read.
    QList<FileError> errors;         ///< One per file that could not be read.
};

/// Reads a project from its files. Files that aren't project files are ignored.
///
/// Only what the model can't represent fails a file: bad JSON, wrong types, unknown enum
/// values, invalid ids, unsupported versions. References that don't resolve, such as a
/// transition to a missing scene, load fine and are left to validation.
ParseResult parseProject(const ProjectFiles& files);

/// Reads an entity that is on no layer yet, such as a preset's: as in a scene file, except
/// that it has no `layer`, since it goes on the layer it is placed on.
/// @throws FormatError
Entity readDetachedEntity(ObjectReader& reader);

/// Writes a project in canonical form: the same project always gives the same bytes.
ProjectFiles serializeProject(const Project& project);

}  // namespace shoal::core
