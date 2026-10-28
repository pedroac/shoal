#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "core/Model.h"
#include "core/ProjectCodec.h"

namespace shoal::core {

struct LoadResult {
    std::optional<Project> project;
    QList<FileError> errors;
    QStringList files;  ///< The project files read, relative to the project folder.
};

/// Reads the project in `directory`: the manifest, flow.json, classes.json,
/// presets/*.json, scenes/*/scene.json and scenes/*/modifiers/*.json. Nothing else is read.
LoadResult loadProject(const QString& directory);

struct SaveResult {
    QList<FileError> errors;
    QStringList files;    ///< Every project file now on disk, to pass to the next save.
    QStringList written;  ///< Files whose bytes changed.
    QStringList removed;  ///< Files of the previous save that no longer belong to the project.
    bool ok() const { return errors.isEmpty(); }
};

/// The project files present in `directory`, relative to it, whether or not they parse.
QStringList findProjectFiles(const QString& directory);

/// Writes `project` into `directory`.
///
/// - A file whose bytes are already on disk is left untouched, so saving an unchanged
///   project touches nothing.
/// - Each file is written atomically: a failed save leaves the previous file in place.
/// - Of the files in `previousFiles` (the `files` of the last load or save), those the
///   project no longer has are removed, along with folders left empty, such as a deleted
///   scene's. Files Shoal didn't load or write are never touched.
SaveResult saveProject(const Project& project, const QString& directory,
                       const QStringList& previousFiles);

}  // namespace shoal::core
