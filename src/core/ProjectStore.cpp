#include "core/ProjectStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace shoal::core {

namespace {

const QString JSON_FILTER = QStringLiteral("*.json");

QStringList findProjectFilesIn(const QDir& directory)
{
    QStringList found;
    for (const QString& name : {paths::MANIFEST, paths::FLOW, paths::CLASSES}) {
        if (QFileInfo(directory.filePath(name)).isFile()) {
            found << name;
        }
    }
    for (const QString& folder : {QStringLiteral("presets"), QStringLiteral("transitions")}) {
        const QDir files(directory.filePath(folder));
        for (const QString& name : files.entryList({JSON_FILTER}, QDir::Files, QDir::Name)) {
            found << folder + u'/' + name;
        }
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

/// Renames, on disk, the files and folders of `previous` that a path in `files` spells with
/// different case, such as a scene renamed from `Boss` to `boss`, and returns `previous` with
/// the new spelling. On a case-insensitive filesystem both spellings are the same file, so
/// without this the save would overwrite the old file in place, under its old name, and then
/// remove it as stale.
QStringList matchCase(QDir root, const ProjectFiles& files, QStringList previous,
                      QList<FileError>& errors)
{
    for (const auto& entry : files) {
        const QStringList segments = entry.first.split(u'/');
        for (qsizetype n = 1; n <= segments.size(); ++n) {
            const QString wanted = segments.first(n).join(u'/');
            QString found;
            for (const QString& old : previous) {
                const QString prefix = old.section(u'/', 0, int(n - 1));
                if (prefix.count(u'/') == n - 1 && prefix != wanted &&
                    prefix.compare(wanted, Qt::CaseInsensitive) == 0) {
                    found = prefix;
                    break;
                }
            }
            if (found.isEmpty()) {
                continue;
            }
            if (QFileInfo::exists(root.filePath(found))) {
                // Through a temporary name, since some filesystems ignore a rename that
                // only changes case.
                const QString temporary = found + QStringLiteral(".shoal-renaming");
                if (!root.rename(found, temporary)) {
                    errors << FileError{found, QStringLiteral("cannot rename to %1").arg(wanted)};
                    return previous;
                }
                if (!root.rename(temporary, wanted)) {
                    root.rename(temporary, found);
                    errors << FileError{found, QStringLiteral("cannot rename to %1").arg(wanted)};
                    return previous;
                }
            }
            for (QString& old : previous) {
                if (old == found || old.startsWith(found + u'/')) {
                    old = wanted + old.mid(found.size());
                }
            }
        }
    }
    return previous;
}

}  // namespace

QStringList findProjectFiles(const QString& directory)
{
    return findProjectFilesIn(QDir(directory));
}

LoadResult loadProject(const QString& directory)
{
    LoadResult result;
    const QDir root(directory);
    ProjectFiles files;
    for (const QString& path : findProjectFilesIn(root)) {
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
    const QStringList previous = matchCase(root, files, previousFiles, result.errors);
    if (!result.errors.isEmpty()) {
        result.files = previousFiles;  // nothing was written
        return result;
    }

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

    for (const QString& path : previous) {
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
