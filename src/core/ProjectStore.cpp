#include "core/ProjectStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace shoal::core {

namespace {

const QString JSON_FILTER = QStringLiteral("*.json");

/// The project files present in `directory`, relative to it.
QStringList findProjectFiles(const QDir& directory)
{
    QStringList found;
    for (const QString& name : {paths::MANIFEST, paths::FLOW, paths::CLASSES}) {
        if (QFileInfo(directory.filePath(name)).isFile()) {
            found << name;
        }
    }
    const QDir presets(directory.filePath(QStringLiteral("presets")));
    for (const QString& name : presets.entryList({JSON_FILTER}, QDir::Files, QDir::Name)) {
        found << QStringLiteral("presets/") + name;
    }
    const QDir scenes(directory.filePath(QStringLiteral("scenes")));
    for (const QString& scene : scenes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString scenePath = QStringLiteral("scenes/%1/").arg(scene);
        if (QFileInfo(directory.filePath(scenePath + QStringLiteral("scene.json"))).isFile()) {
            found << scenePath + QStringLiteral("scene.json");
        }
        const QDir modifiers(directory.filePath(scenePath + QStringLiteral("modifiers")));
        for (const QString& name : modifiers.entryList({JSON_FILTER}, QDir::Files, QDir::Name)) {
            found << scenePath + QStringLiteral("modifiers/") + name;
        }
    }
    return found;
}

/// Removes the folders above `path` that are left empty, up to the project folder.
void removeEmptyParents(const QDir& directory, const QString& path)
{
    QString folder = QFileInfo(path).path();
    while (!folder.isEmpty() && folder != QStringLiteral(".")) {
        if (!directory.rmdir(folder)) {  // fails while the folder still has files
            return;
        }
        folder = QFileInfo(folder).path();
    }
}

}  // namespace

LoadResult loadProject(const QString& directory)
{
    LoadResult result;
    const QDir root(directory);
    ProjectFiles files;
    for (const QString& path : findProjectFiles(root)) {
        QFile file(root.filePath(path));
        if (!file.open(QIODevice::ReadOnly)) {
            result.errors << FileError{path,
                                       QStringLiteral("cannot read: %1").arg(file.errorString())};
            continue;
        }
        files[path] = file.readAll();
        result.files << path;
    }
    if (!result.errors.isEmpty()) {
        return result;
    }
    ParseResult parsed = parseProject(files);
    result.project = std::move(parsed.project);
    result.errors = std::move(parsed.errors);
    return result;
}

SaveResult saveProject(const Project& project, const QString& directory,
                       const QStringList& previousFiles)
{
    SaveResult result;
    const QDir root(directory);
    const ProjectFiles files = serializeProject(project);

    for (const auto& [path, bytes] : files) {
        result.files << path;
        const QString target = root.filePath(path);
        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes) {
            continue;
        }
        existing.close();
        if (!root.mkpath(QFileInfo(path).path())) {
            result.errors << FileError{path, QStringLiteral("cannot create its folder")};
            continue;
        }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
            !file.commit()) {
            result.errors << FileError{path,
                                       QStringLiteral("cannot write: %1").arg(file.errorString())};
            continue;
        }
        result.written << path;
    }

    for (const QString& path : previousFiles) {
        if (files.contains(path) || !isProjectFile(path)) {
            continue;
        }
        if (QFile::exists(root.filePath(path)) && !QFile::remove(root.filePath(path))) {
            result.errors << FileError{path, QStringLiteral("cannot remove the old file")};
            result.files << path;  // still on disk, so the next save tries again
            continue;
        }
        result.removed << path;
        removeEmptyParents(root, path);
    }
    return result;
}

}  // namespace shoal::core
