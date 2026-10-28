#include <QTest>

#include "core/Version.h"

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void versionMatchesVersionFile()
    {
        QCOMPARE(shoal::core::version(), QStringLiteral(SHOAL_EXPECTED_VERSION));
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
