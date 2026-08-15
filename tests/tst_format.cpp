#include "formatutil.h"

#include <QTest>

class TestFormat : public QObject
{
	Q_OBJECT
private slots:
	void bytes_below_kb_in_raw_bytes();
	void bytes_at_kb_boundary();
	void bytes_at_mb_boundary();
	void bytes_at_gb_boundary();
	void bytes_at_tb_boundary();
	void bytes_clamps_negative_to_zero();
};

void TestFormat::bytes_below_kb_in_raw_bytes()
{
	QCOMPARE(Format::bytes(0), QStringLiteral("0 B"));
	QCOMPARE(Format::bytes(1), QStringLiteral("1 B"));
	QCOMPARE(Format::bytes(999), QStringLiteral("999 B"));
}

void TestFormat::bytes_at_kb_boundary()
{
	QCOMPARE(Format::bytes(1000), QStringLiteral("1 KB"));
}

void TestFormat::bytes_at_mb_boundary()
{
	QCOMPARE(Format::bytes(qint64(1000) * 1000), QStringLiteral("1.0 MB"));
	QCOMPARE(Format::bytes(qint64(1500) * 1000), QStringLiteral("1.5 MB"));
}

void TestFormat::bytes_at_gb_boundary()
{
	QCOMPARE(Format::bytes(qint64(1000) * 1000 * 1000), QStringLiteral("1.0 GB"));
}

void TestFormat::bytes_at_tb_boundary()
{
	QCOMPARE(Format::bytes(qint64(1000) * 1000 * 1000 * 1000), QStringLiteral("1.0 TB"));
}

void TestFormat::bytes_clamps_negative_to_zero()
{
	QCOMPARE(Format::bytes(-1), QStringLiteral("0 B"));
	QCOMPARE(Format::bytes(-12345), QStringLiteral("0 B"));
}

QTEST_APPLESS_MAIN(TestFormat)
#include "tst_format.moc"