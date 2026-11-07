#include "filecheck.h"
#include "volumeidentity.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	QString makeFile(const QString &directory, const QString &name = QStringLiteral("clip.mxf"))
	{
		const auto path = directory + QLatin1Char('/') + name;
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write("physical fixture") != 16)
			return {};
		return path;
	}
}

class TestFileCheck : public QObject
{
	Q_OBJECT

private slots:
	void physicalOnlyDoesNotRequireReadableMedia()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OpItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.headerIdentity = OpHeaderIdentity{};
		QString error;
		auto source = OpFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QVERIFY(error.isEmpty());
		QVERIFY(source->io().isOpen());
	}

	void rejectsChangedPhysicalFacts_data()
	{
		QTest::addColumn<int>("difference");
		QTest::newRow("native identity") << 0;
		QTest::newRow("journal size") << 1;
		QTest::newRow("journal modification time") << 2;
		QTest::newRow("selected size") << 3;
		QTest::newRow("selected modification time") << 4;
	}

	void rejectsChangedPhysicalFacts()
	{
		QFETCH(int, difference);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OpItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		QString error;
		auto source = OpFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto current = source->stamp();
		auto expected = current;
		switch (difference)
		{
		case 0:
			expected.fileId += QStringLiteral("-replaced");
			break;
		case 1:
			++expected.size;
			break;
		case 2:
			++expected.modified;
			break;
		case 3:
			++item.bytes;
			break;
		case 4:
			item.modifiedMs = QFileInfo(item.src).lastModified().toMSecsSinceEpoch() - 1;
			break;
		}
		QVERIFY(!FileCheck::sourceMatches(item, *source, expected, current, error));
		QCOMPARE(error, QStringLiteral("The file changed since it was selected or journalled; rescan before proceeding."));
	}

	void preservesScanLocationMessages_data()
	{
		QTest::addColumn<int>("difference");
		QTest::addColumn<QString>("message");
		QTest::newRow("path") << 0 << QStringLiteral("The selected path differs from its scan receipt. Rescan before proceeding.");
		QTest::newRow("missing volume") << 1 << QStringLiteral("The scan did not establish the source volume identity. Rescan before proceeding.");
		QTest::newRow("missing date") << 2 << QStringLiteral("The scan did not establish the source modification time. Rescan before proceeding.");
		QTest::newRow("different volume") << 3 << QStringLiteral("The source volume identity is unavailable or differs from the scan. Rescan before proceeding.");
	}

	void preservesScanLocationMessages()
	{
		QFETCH(int, difference);
		QFETCH(QString, message);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OpItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.scanPath = item.src;
		item.modifiedMs = QFileInfo(item.src).lastModified().toMSecsSinceEpoch();
		item.scanVolumeIdentifier = VolumeIdentity::capture(item.src).identifier();
		QVERIFY(!item.scanVolumeIdentifier.isEmpty());
		switch (difference)
		{
		case 0:
			item.scanPath += QStringLiteral("-other");
			break;
		case 1:
			item.scanVolumeIdentifier.clear();
			break;
		case 2:
			item.modifiedMs = -1;
			break;
		case 3:
			item.scanVolumeIdentifier += QStringLiteral("-other");
			break;
		}
		QString error;
		auto source = OpFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(!FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QCOMPARE(error, message);
	}

	void explicitEmptyReceiptDoesNotImposeSelectedIds()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OpItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.fileMobId = QStringLiteral("selected database identity");
		item.masterMobId = QStringLiteral("selected clip identity");
		item.headerIdentity = OpHeaderIdentity{};
		QString error;
		auto source = OpFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		item.headerIdentity.reset(); // Earlier jobs retain their original fallback requirement.
		QVERIFY(!FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QCOMPARE(error, QStringLiteral("The file's Avid identity is missing or differs from the scan. Rescan before proceeding."));
		QVERIFY(source->io().isOpen());
	}

	void retainsRecordedRescanInstruction()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OpItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.headerIdentity = OpHeaderIdentity{{}, {}, QStringLiteral("Recorded rescan instruction.")};
		QString error;
		auto source = OpFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(!FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QCOMPARE(error, item.headerIdentity->unavailableReason);
	}
};

QTEST_GUILESS_MAIN(TestFileCheck)
#include "tst_filecheck.moc"
