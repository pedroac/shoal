#include "assets/FontCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>

namespace shoal::assets {

FontCache::FontCache(QObject* parent) : QObject(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(RELOAD_DELAY_MS);
    connect(&m_reloadTimer, &QTimer::timeout, this, &FontCache::reloadPending);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FontCache::onFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &FontCache::onDirectoryChanged);
}

FontCache::~FontCache()
{
    for (const Entry& entry : std::as_const(m_entries)) {
        if (entry.id >= 0) {
            QFontDatabase::removeApplicationFont(entry.id);
        }
    }
}

void FontCache::setFonts(const QMap<QString, QString>& fonts, const QString& baseDirectory)
{
    const QDir base(baseDirectory);
    for (const QString& alias : m_entries.keys()) {
        if (!fonts.contains(alias)) {
            if (m_entries[alias].id >= 0) {
                QFontDatabase::removeApplicationFont(m_entries[alias].id);
            }
            m_entries.remove(alias);
            emit fontChanged(alias);
        }
    }
    for (auto it = fonts.cbegin(); it != fonts.cend(); ++it) {
        const QString file = QDir::cleanPath(base.filePath(*it));
        const auto entry = m_entries.find(it.key());
        if (entry != m_entries.end() && entry->file == file) {
            continue;
        }
        if (entry != m_entries.end() && entry->id >= 0) {
            QFontDatabase::removeApplicationFont(entry->id);
        }
        m_entries[it.key()] = Entry{file, -1, {}, {}};
        load(it.key());
    }
    updateWatches();
}

QString FontCache::family(const QString& alias) const
{
    const auto it = m_entries.find(alias);
    return it == m_entries.end() ? QString() : it->family;
}

QString FontCache::file(const QString& alias) const
{
    const auto it = m_entries.find(alias);
    return it == m_entries.end() ? QString() : it->file;
}

QString FontCache::error(const QString& alias) const
{
    const auto it = m_entries.find(alias);
    return it == m_entries.end() ? QString() : it->error;
}

void FontCache::load(const QString& alias)
{
    Entry& entry = m_entries[alias];
    QFile file(entry.file);
    if (!file.open(QIODevice::ReadOnly)) {
        entry.error = file.exists() ? file.errorString() : tr("The file doesn't exist.");
        emit fontChanged(alias);
        return;
    }
    // From the bytes, so a font written again replaces the one registered.
    const int id = QFontDatabase::addApplicationFontFromData(file.readAll());
    const QStringList families =
        id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    if (families.isEmpty()) {
        if (id >= 0) {
            QFontDatabase::removeApplicationFont(id);
        }
        entry.error = tr("It isn't a font Qt can read.");  // the last good one, if any, stays
        emit fontChanged(alias);
        return;
    }
    if (entry.id >= 0) {
        QFontDatabase::removeApplicationFont(entry.id);
    }
    entry.id = id;
    entry.family = families.first();
    entry.error.clear();
    emit fontChanged(alias);
}

void FontCache::onFileChanged(const QString& path)
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->file == path) {
            m_pending.insert(it.key());
        }
    }
    m_reloadTimer.start();  // restarts, so a burst of writes reloads once
}

void FontCache::onDirectoryChanged(const QString& path)
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

void FontCache::reloadPending()
{
    const QSet<QString> pending = std::exchange(m_pending, {});
    for (const QString& alias : pending) {
        if (m_entries.contains(alias)) {
            load(alias);
        }
    }
    updateWatches();
}

void FontCache::updateWatches()
{
    // A file replaced (written anew, renamed over the old) ends its watch, so the files are
    // watched again after every load; a missing one's folder, to see it appear.
    QStringList wanted;
    for (const Entry& entry : std::as_const(m_entries)) {
        if (QFileInfo::exists(entry.file)) {
            wanted << entry.file;
        }
        else if (QFileInfo(entry.file).dir().exists()) {
            wanted << QFileInfo(entry.file).absolutePath();
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
        if (!watched.contains(file) && !missing.contains(file)) {
            missing << file;
        }
    }
    if (!missing.isEmpty()) {
        m_watcher.addPaths(missing);
    }
}

}  // namespace shoal::assets
