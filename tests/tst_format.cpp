#include "formatutil.h"

#include <QTest>

class TestFormat : public QObject
{
	Q_OBJECT
private slots:
	void bytes_data();
	void bytes();
};

void TestFormat::bytes_data()
{
	QTest::addColumn<qint64>("size");
	QTest::addColumn<QString>("expected");
	QTest::newRow("zero") << qint64(0) << QStringLiteral("0 B");
	QTest::newRow("one-byte") << qint64(1) << QStringLiteral("1 B");
	QTest::newRow("below-kb") << qint64(999) << QStringLiteral("999 B");
	QTest::newRow("kb") << qint64(1000) << QStringLiteral("1 KB");
	QTest::newRow("mb") << qint64(1000000) << QStringLiteral("1.0 MB");
	QTest::newRow("fractional-mb") << qint64(1500000) << QStringLiteral("1.5 MB");
	QTest::newRow("gb") << qint64(1000000000) << QStringLiteral("1.0 GB");
	QTest::newRow("tb") << qint64(1000000000000) << QStringLiteral("1.0 TB");
	QTest::newRow("negative-one") << qint64(-1) << QStringLiteral("0 B");
	QTest::newRow("negative-large") << qint64(-12345) << QStringLiteral("0 B");
}

void TestFormat::bytes()
{
	QFETCH(qint64, size);
	QFETCH(QString, expected);
	QCOMPARE(Format::bytes(size), expected);
}

QTEST_APPLESS_MAIN(TestFormat)
#include "tst_format.moc"
