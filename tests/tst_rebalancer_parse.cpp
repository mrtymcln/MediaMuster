#include "rebalancer.h"
#include "rebalanceplan.h"

#include <QTest>

class TestRebalancerParse : public QObject
{
	Q_OBJECT
private slots:
	void parseFolderName_plain_number();
	void parseFolderName_with_host_prefix();
	void parseFolderName_rejects_quarantined();
	void parseFolderName_rejects_leading_dot();
	void parseFolderName_rejects_zero_padded();
	void parseFolderName_rejects_zero();
	void parseFolderName_rejects_negative();
	void parseFolderName_rejects_non_numeric_tail();
};

void TestRebalancerParse::parseFolderName_plain_number()
{
	const auto id = Rebalancer::parseFolderName(QStringLiteral("5"));
	QVERIFY(id.has_value());
	QCOMPARE(id->prefix, QString());
	QCOMPARE(id->n, 5);
	QCOMPARE(id->display(), QStringLiteral("5"));
}

void TestRebalancerParse::parseFolderName_with_host_prefix()
{
	const auto id = Rebalancer::parseFolderName(QStringLiteral("MartysiMac.42"));
	QVERIFY(id.has_value());
	QCOMPARE(id->prefix, QStringLiteral("MartysiMac"));
	QCOMPARE(id->n, 42);
	QCOMPARE(id->display(), QStringLiteral("MartysiMac.42"));
}

void TestRebalancerParse::parseFolderName_rejects_quarantined()
{
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("Quarantined Files")).has_value());
}

void TestRebalancerParse::parseFolderName_rejects_leading_dot()
{
	// '.5' parses tail '5' as n=5, but display would render as '5'
	// (empty prefix), which doesn't match the original '.5'.
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral(".5")).has_value());
}

void TestRebalancerParse::parseFolderName_rejects_zero_padded()
{
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("05")).has_value());
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("MartysiMac.005")).has_value());
}

void TestRebalancerParse::parseFolderName_rejects_zero()
{
	// Folder "0" is non-canonical for Avid. Rejected by the n > 0 guard.
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("0")).has_value());
}

void TestRebalancerParse::parseFolderName_rejects_negative()
{
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("-1")).has_value());
}

void TestRebalancerParse::parseFolderName_rejects_non_numeric_tail()
{
	QVERIFY(!Rebalancer::parseFolderName(QStringLiteral("MartysiMac.abc")).has_value());
}

QTEST_APPLESS_MAIN(TestRebalancerParse)
#include "tst_rebalancer_parse.moc"