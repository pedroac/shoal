#pragma once

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "core/ProjectCodec.h"

/// Test helpers shared by the core suites. SHOAL_FIXTURES_DIR is set by tests/CMakeLists.txt.
namespace support {

inline QString fixturePath(const QString& name)
{
    return QStringLiteral(SHOAL_FIXTURES_DIR "/") + name;
}

/// Every file under `directory`, by path relative to it.
inline shoal::core::ProjectFiles readTree(const QString& directory)
{
    shoal::core::ProjectFiles files;
    const QDir root(directory);
    QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            files[root.relativeFilePath(path)] = file.readAll();
        }
    }
    return files;
}

/// Writes `files` under `directory`, creating folders as needed.
inline bool writeTree(const QString& directory, const shoal::core::ProjectFiles& files)
{
    const QDir root(directory);
    for (const auto& [path, bytes] : files) {
        if (!root.mkpath(QFileInfo(path).path())) {
            return false;
        }
        QFile file(root.filePath(path));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return false;
        }
    }
    return true;
}

/// The keys of `files`, for readable comparisons.
inline QStringList fileNames(const shoal::core::ProjectFiles& files)
{
    QStringList names;
    for (const auto& entry : files) {
        names << entry.first;
    }
    return names;
}

}  // namespace support
