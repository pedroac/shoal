#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <memory>

#include "assets/SpriteSheet.h"

namespace shoal::assets {

/// The sheets a project declares, by alias, loaded and kept current: when sprat-gui or
/// sprat-cli writes a sheet or one of its atlases again, the sheet reloads and
/// sheetChanged() tells the views to redraw.
///
/// A sheet that doesn't exist yet loads as soon as it is written.
///
/// A reload that fails, such as one that catches a file half written, keeps the sheet
/// already loaded, so the canvas doesn't flicker to placeholders.
class SheetCache : public QObject {
    Q_OBJECT
public:
    explicit SheetCache(QObject* parent = nullptr);

    /// Declares the sheets: alias -> path of the .sprat file, relative to `baseDirectory`
    /// (the project folder). Sheets whose file didn't change are kept as loaded.
    void setSheets(const QMap<QString, QString>& sheets, const QString& baseDirectory);

    QStringList aliases() const { return m_entries.keys(); }
    /// The loaded sheet, or null if it never loaded.
    std::shared_ptr<const Sheet> sheet(const QString& alias) const;
    /// Why the sheet didn't load last time, or empty.
    QString error(const QString& alias) const;
    /// Loads every sheet again now.
    void reloadAll();

    /// How long to wait after a file changes before reloading, so a writer can finish.
    static constexpr int RELOAD_DELAY_MS = 150;

signals:
    void sheetChanged(const QString& alias);

private:
    struct Entry {
        QString file;
        std::shared_ptr<const Sheet> sheet;
        QString error;
    };

    void load(const QString& alias);
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void reloadPending();
    void updateWatches();

    QMap<QString, Entry> m_entries;
    QFileSystemWatcher m_watcher;
    QSet<QString> m_pending;  ///< Aliases waiting for the reload timer.
    QTimer m_reloadTimer;
};

}  // namespace shoal::assets
