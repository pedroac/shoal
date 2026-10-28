#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "Support.h"
#include "core/ProjectStore.h"
#include "core/ProjectTemplates.h"

using namespace shoal::core;

class ProjectStoreTests : public QObject {
    Q_OBJECT
private:
    /// A copy of the basic fixture in a temporary folder, plus files that aren't Shoal's.
    bool makeProject(QTemporaryDir& dir)
    {
        ProjectFiles files = support::readTree(support::fixturePath(QStringLiteral("basic")));
        files[QStringLiteral("art/hero.sprat")] = "sprat-raw 2\n";
        files[QStringLiteral("scenes/level_2/notes.txt")] = "keep me\n";
        return dir.isValid() && support::writeTree(dir.path(), files);
    }

private slots:
    void loadsAProjectFolder()
    {
        QTemporaryDir dir;
        QVERIFY(makeProject(dir));
        const LoadResult loaded = loadProject(dir.path());
        QVERIFY(loaded.errors.isEmpty());
        QVERIFY(loaded.project);
        QCOMPARE(loaded.files.size(), 10);
        QVERIFY(!loaded.files.contains(QStringLiteral("art/hero.sprat")));
        QCOMPARE(loaded.project->scenes.size(), 4);
    }

    void reportsAFolderThatIsNotAProject()
    {
        QTemporaryDir dir;
        const LoadResult loaded = loadProject(dir.path());
        QVERIFY(!loaded.project);
        QCOMPARE(loaded.errors.size(), 1);
        QCOMPARE(loaded.errors[0].file, paths::MANIFEST);
    }

    void savingAnUnchangedProjectTouchesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(makeProject(dir));
        const LoadResult loaded = loadProject(dir.path());
        const QDateTime before = QFileInfo(dir.filePath(paths::FLOW)).lastModified();

        const SaveResult saved = saveProject(*loaded.project, dir.path(), loaded.files);
        QVERIFY(saved.ok());
        QVERIFY(saved.written.isEmpty());
        QVERIFY(saved.removed.isEmpty());
        QCOMPARE(QFileInfo(dir.filePath(paths::FLOW)).lastModified(), before);
    }

    void writesOnlyChangedFiles()
    {
        QTemporaryDir dir;
        QVERIFY(makeProject(dir));
        LoadResult loaded = loadProject(dir.path());
        loaded.project->manifest.name = QStringLiteral("Renamed Game");

        const SaveResult saved = saveProject(*loaded.project, dir.path(), loaded.files);
        QVERIFY(saved.ok());
        QCOMPARE(saved.written, QStringList{paths::MANIFEST});
        QCOMPARE(loadProject(dir.path()).project->manifest.name, QStringLiteral("Renamed Game"));
    }

    void removesWhatTheProjectNoLongerHas()
    {
        QTemporaryDir dir;
        QVERIFY(makeProject(dir));
        LoadResult loaded = loadProject(dir.path());
        loaded.project->scenes.remove(QStringLiteral("level_1"));  // with both its modifiers
        loaded.project->scenes.remove(QStringLiteral("level_2"));  // its folder has notes.txt
        loaded.project->presets.clear();
        loaded.project->classes.reset();

        const SaveResult saved = saveProject(*loaded.project, dir.path(), loaded.files);
        QVERIFY(saved.ok());
        QCOMPARE(
            saved.removed,
            (QStringList{paths::CLASSES, paths::preset(QStringLiteral("enemy_grunt")),
                         paths::scene(QStringLiteral("level_1")),
                         paths::modifier(QStringLiteral("level_1"), QStringLiteral("altar_reveal")),
                         paths::modifier(QStringLiteral("level_1"), QStringLiteral("earthquake")),
                         paths::scene(QStringLiteral("level_2"))}));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("scenes/level_1"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("presets"))));
        // Files Shoal doesn't own stay, and so does the folder that holds them.
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("scenes/level_2/notes.txt"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("art/hero.sprat"))));
        QVERIFY(!saved.files.contains(paths::scene(QStringLiteral("level_1"))));

        const LoadResult reloaded = loadProject(dir.path());
        QVERIFY(reloaded.project);
        QCOMPARE(*reloaded.project, *loaded.project);
    }

    void savesANewProjectIntoAnEmptyFolder()
    {
        QTemporaryDir dir;
        const Project project = newProject(QStringLiteral("New Game"), ProjectTemplate::Default);
        const SaveResult saved = saveProject(project, dir.path(), {});
        QVERIFY(saved.ok());
        QCOMPARE(saved.written, (QStringList{paths::CLASSES, paths::FLOW, paths::MANIFEST,
                                             paths::scene(QStringLiteral("main"))}));
        const LoadResult loaded = loadProject(dir.path());
        QVERIFY(loaded.project);
        QCOMPARE(*loaded.project, project);
    }
};

QTEST_GUILESS_MAIN(ProjectStoreTests)
#include "ProjectStoreTests.moc"
