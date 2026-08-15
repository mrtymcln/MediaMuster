#include "pmrkey.h"

#include <QTest>

class TestPmrKey : public QObject
{
	Q_OBJECT
private slots:
	void primary_lowercases();
	void primary_normalizes_nfd_to_nfc();
	void primary_passes_through_already_normalised();
	void fallback_strips_trailing_extension();
	void fallback_replaces_internal_dots_with_underscores();
	void fallback_passes_through_extensionless_input();
	void fallback_handles_leading_dot();
};

void TestPmrKey::primary_lowercases()
{
	QCOMPARE(PmrKey::primary(QStringLiteral("MyClip.MXF")), QStringLiteral("myclip.mxf"));
}

void TestPmrKey::primary_normalizes_nfd_to_nfc()
{
	// 'café.mxf': NFD (e + combining acute) vs NFC (precomposed é).
	const QString nfd = QString::fromUtf8("cafe\xCC\x81.mxf");
	const QString nfc = QString::fromUtf8("caf\xC3\xA9.mxf");
	QCOMPARE(PmrKey::primary(nfd), nfc);
	QCOMPARE(PmrKey::primary(nfd), PmrKey::primary(nfc));
}

void TestPmrKey::primary_passes_through_already_normalised()
{
	QCOMPARE(PmrKey::primary(QStringLiteral("simple.mxf")), QStringLiteral("simple.mxf"));
}

void TestPmrKey::fallback_strips_trailing_extension()
{
	QCOMPARE(PmrKey::fallback(QStringLiteral("myclip.mxf")), QStringLiteral("myclip"));
}

void TestPmrKey::fallback_replaces_internal_dots_with_underscores()
{
	// Avid renames 'myclip.tail.mxf' to 'myclip_tail' in the PMR.
	QCOMPARE(PmrKey::fallback(QStringLiteral("myclip.tail.mxf")), QStringLiteral("myclip_tail"));
}

void TestPmrKey::fallback_passes_through_extensionless_input()
{
	QCOMPARE(PmrKey::fallback(QStringLiteral("noext")), QStringLiteral("noext"));
}

void TestPmrKey::fallback_handles_leading_dot()
{
	QCOMPARE(PmrKey::fallback(QStringLiteral(".hidden")), QStringLiteral("_hidden"));
}

QTEST_APPLESS_MAIN(TestPmrKey)
#include "tst_pmrkey.moc"