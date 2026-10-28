#include "assets/SheetCache.h"

#include <QDir>
#include <QFileInfo>

namespace shoal::assets {

SheetCache::SheetCache(QObject* parent) : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(RELOAD_DELAY_MS);
    connect(&m_reloadTimer, &QTimer::timeout, this, &SheetCache::reloadPending);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &SheetCache::onFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &SheetCache::onDirectoryChanged);
}

void SheetCache::setSheets(const QMap<QString, QString>& sheets, const QString& baseDirectory)
{
    const QDir base(baseDirectory);
    for (const QString& alias : m_entries.keys()) {
        if (!sheets.contains(alias)) {
            m_entries.remove(alias);
            emit sheetChanged(alias);
        }
    }
    for (auto it = sheets.cbegin(); it != sheets.cend(); ++it) {
        const QString file = QDir::cleanPath(base.filePath(*it));
        if (m_entries.contains(it.key()) && m_entries[it.key()].file == file) {
            continue;
        }
        m_entries[it.key()] = Entry{file, nullptr, {}};
        load(it.key());
    }
    updateWatches();
}

std::shared_ptr<const Sheet> SheetCache::sheet(const QString& alias) const
{
    const auto it = m_entries.find(alias);
    return it == m_entries.end() ? nullptr : it->sheet;
}

QString SheetCache::error(const QString& alias) const
{
    const auto it = m_entries.find(alias);
    return it == m_entries.end() ? QString() : it->error;
}

void SheetCache::reloadAll()
{
    for (const QString& alias : m_entries.keys()) {
        load(alias);
    }
    updateWatches();
}

void SheetCache::load(const QString& alias)
{
    Entry& entry = m_entries[alias];
    SheetLoad loaded = loadSheet(entry.file);
    if (loaded.sheet) {
        entry.sheet = std::move(loaded.sheet);
        entry.error.clear();
    }
    else {
        entry.error = loaded.error;  // the last good sheet, if any, stays
    }
    emit sheetChanged(alias);
}

void SheetCache::onFileChanged(const QString& path)
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const bool atlas = it->sheet && it->sheet->atlasFiles.contains(path);
        if (it->file == path || atlas) {
            m_pending.insert(it.key());
        }
    }
    m_reloadTimer.start();  // restarts, so a burst of writes reloads once
}

void SheetCache::onDirectoryChanged(const QString& path)
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (QFileInfo(it->file).absolutePath() == path && QFileInfo::exists(it->file) &&
            !m_watcher.files().contains(it->file)) {
            m_pending.insert(it.key());  // it has just been written
        }
    }
    if (!m_pending.isEmpty()) {
        m_reloadTimer.start();
    }
}

void SheetCache::reloadPending()
{
    const QSet<QString> pending = std::exchange(m_pending, {});
    for (const QString& alias : pending) {
        if (m_entries.contains(alias)) {
            load(alias);
        }
    }
    updateWatches();
}

void SheetCache::updateWatches()
{
    // Writers often replace a file (write a new one, rename it over the old), which ends
    // the watch, so the files are watched again after every load.
    QStringList wanted;
    for (const Entry& entry : std::as_const(m_entries)) {
        if (QFileInfo::exists(entry.file)) {
            wanted << entry.file;
        }
        else if (QFileInfo(entry.file).dir().exists()) {
            wanted << QFileInfo(entry.file).absolutePath();  // to see it appear
        }
        if (entry.sheet) {
            wanted << entry.sheet->atlasFiles;
        }
    }
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    QStringList stale;
    for (const QString& file : watched) {
        if (!wanted.contains(file)) {
            stale << file;
        }
    }
    if (!stale.isEmpty()) {
        m_watcher.removePaths(stale);
    }
    QStringList missing;
    for (const QString& file : wanted) {
        if (!watched.contains(file) && !missing.contains(file) && QFileInfo::exists(file)) {
            missing << file;
        }
    }
    if (!missing.isEmpty()) {
        m_watcher.addPaths(missing);
    }
}

}  // namespace shoal::assets
