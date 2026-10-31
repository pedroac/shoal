#pragma once

#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace shoal::assets {

/// The fonts a project declares, by alias, each registered with QFontDatabase from its file,
/// so the system's fonts never stand in for it, and kept current: a font file written again
/// registers again, and fontChanged() tells the views to redraw.
///
/// A font that doesn't exist yet loads as soon as it is written. A reload that fails keeps
/// the font already loaded.
class FontCache : public QObject {
    Q_OBJECT
public:
    explicit FontCache(QObject* parent = nullptr);
    ~FontCache() override;

    /// Declares the fonts: alias -> path of the .ttf or .otf file, relative to
    /// `baseDirectory` (the project folder). Those whose file didn't change are kept.
    void setFonts(const QMap<QString, QString>& fonts, const QString& baseDirectory);

    QStringList aliases() const { return m_entries.keys(); }
    /// The family to draw `alias` with, or empty if it never loaded.
    QString family(const QString& alias) const;
    /// The file, resolved, or empty for an alias it doesn't have.
    QString file(const QString& alias) const;
    /// Why the font didn't load last time, or empty.
    QString error(const QString& alias) const;

    /// How long to wait after a file changes before reloading, so a writer can finish.
    static constexpr int RELOAD_DELAY_MS = 150;

signals:
    void fontChanged(const QString& alias);

private:
    struct Entry {
        QString file;
        int id = -1;  ///< QFontDatabase's, or -1.
        QString family;
        QString error;
    };

    void load(const QString& alias);
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void reloadPending();
    void updateWatches();

    QMap<QString, Entry> m_entries;
    QFileSystemWatcher m_watcher;
    QSet<QString> m_pending;
    QTimer m_reloadTimer;
};

}  // namespace shoal::assets
