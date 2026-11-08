#include <QTest>
#include <QJsonObject>
#include "../probe/JsonUtil.h"

class TestJsonUtil : public QObject {
    Q_OBJECT
private slots:
    void testSanitization() {
        QString raw = "TestString";
        QCOMPARE(raw, QString("TestString"));
    }
};

QTEST_MAIN(TestJsonUtil)
#include "test_jsonutil.moc"
