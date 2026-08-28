#include "pmrkey.h"

#include <QTest>

class TestPmrKey : public QObject
{
	Q_OBJECT
private slots:
	void primary_lowercases();
	void primary_normalizes_nfd_to_nfc();
	void primary_passes_through_already_normalised();
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

QTEST_APPLESS_MAIN(TestPmrKey)
#include "tst_pmrkey.moc"