#include <QRegularExpression>
#include <QTest>

#include "assets/SpratLibrary.h"

class AssetsTests : public QObject {
    Q_OBJECT
private slots:
    void linksSpratLib()
    {
        const QString version = shoal::assets::spratLibraryVersion();
        QVERIFY2(
            QRegularExpression(QStringLiteral("^v\\d+\\.\\d+\\.\\d+$")).match(version).hasMatch(),
            qPrintable(version));
    }
};

QTEST_GUILESS_MAIN(AssetsTests)
#include "AssetsTests.moc"
