#include "operations/file_check.h"
#include "storage/volume_identity.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// MARK: - Test helpers

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

// MARK: - Test cases

class TestFileCheck : public QObject
{
	Q_OBJECT

private slots:
	void openedFileChecksKeepObjectAndPathTogether()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OperationItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.headerIdentity = OperationHeaderIdentity{};
		QString error;
		auto source = OperationFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto expected = source->stamp();
		const int descriptor = source->io().handle();
		QVERIFY(FileCheck::unchanged(expected, expected));
		QVERIFY(FileCheck::unchanged(expected, item.src));
		QVERIFY(FileCheck::unchanged(expected, *source, item.src));

		const auto moved = directory.filePath(QStringLiteral("moved.mxf"));
		QCOMPARE(source->relocate(item.src, moved, error), OperationFile::Relocation::Moved);
		QCOMPARE(makeFile(directory.path()), item.src);
		const auto captured = source->stamp();
		// The captured observation still describes the opened original. Only
		// checks of the old path must reject the new object at that name.
		QVERIFY(FileCheck::unchanged(expected, captured));
		QVERIFY(!FileCheck::sameObject(expected, OperationFile::inspect(item.src)));
		QVERIFY(!FileCheck::unchanged(expected, item.src));
		QVERIFY(!FileCheck::unchanged(expected, *source, item.src));
		QVERIFY(FileCheck::unchanged(expected, moved));
		QVERIFY(FileCheck::unchanged(expected, *source, moved));
		QVERIFY(!FileCheck::sourceMatches(item, *source, expected, captured, error));
		QCOMPARE(error, QStringLiteral("The file changed since it was selected or journalled; rescan before proceeding."));
		QVERIFY(source->io().isOpen());
		QCOMPARE(source->io().handle(), descriptor);
	}

	void identityOnlyAndSizeChecksKeepTheirDifferentMeanings()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = makeFile(directory.path());
		QVERIFY(!path.isEmpty());
		QString error;
		auto source = OperationFile::openWritableExisting(path, error);
		QVERIFY2(source, qPrintable(error));
		const auto before = source->stamp();
		QVERIFY(FileCheck::hasSize(before, 16));
		QCOMPARE(source->io().write(QByteArray(32, 'x')), qint64(32));
		QVERIFY(source->io().flush());
		const auto after = source->stamp();
		QVERIFY(FileCheck::sameObject(before, after));
		QVERIFY(!FileCheck::unchanged(before, after));
		QVERIFY(!FileCheck::hasSize(after, 16));
		QVERIFY(FileCheck::hasSize(after, 32));
		QVERIFY(source->io().isOpen());
	}

	void unknownStampCannotQualifyAnExistingFile()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = makeFile(directory.path());
		QVERIFY(!path.isEmpty());
		QString error;
		auto source = OperationFile::open(path, false, error);
		QVERIFY2(source, qPrintable(error));
		const OperationStamp unknown;
		QVERIFY(!FileCheck::sameObject(unknown, unknown));
		QVERIFY(!FileCheck::sameObject(unknown, source->stamp()));
		QVERIFY(!FileCheck::unchanged(unknown, source->stamp()));
		QVERIFY(!FileCheck::unchanged(unknown, path));
		QVERIFY(!FileCheck::unchanged(unknown, *source, path));
	}

	// MARK: - Copy and scan receipt validation

	void completedCopyCheckPreservesItsMessageAndPriorDiagnostic_data()
	{
		QTest::addColumn<bool>("replace");
		QTest::newRow("missing") << false;
		QTest::newRow("replaced") << true;
	}

	void completedCopyCheckPreservesItsMessageAndPriorDiagnostic()
	{
		QFETCH(bool, replace);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const auto path = makeFile(directory.path());
		QVERIFY(!path.isEmpty());
		const auto expected = OperationFile::inspect(path);
		QString error = QStringLiteral("Earlier diagnostic");
		QVERIFY(FileCheck::completedCopyMatches(path, expected, error));
		QCOMPARE(error, QStringLiteral("Earlier diagnostic"));
		QVERIFY(QFile::rename(path, directory.filePath(QStringLiteral("kept-copy.mxf"))));
		if (replace)
			QCOMPARE(makeFile(directory.path()), path);
		QVERIFY(!FileCheck::completedCopyMatches(path, expected, error));
		QCOMPARE(error, QStringLiteral("The completed destination is missing or changed: ") + path);
	}

	void physicalOnlyDoesNotRequireReadableMedia()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OperationItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.headerIdentity = OperationHeaderIdentity{};
		QString error;
		auto source = OperationFile::open(item.src, false, error);
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
		OperationItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		QString error;
		auto source = OperationFile::open(item.src, false, error);
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
		OperationItem item;
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
		auto source = OperationFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(!FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QCOMPARE(error, message);
	}

	void explicitEmptyReceiptDoesNotImposeSelectedIds()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		OperationItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.fileMobId = QStringLiteral("selected database identity");
		item.masterMobId = QStringLiteral("selected clip identity");
		item.headerIdentity = OperationHeaderIdentity{};
		QString error;
		auto source = OperationFile::open(item.src, false, error);
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
		OperationItem item;
		item.src = makeFile(directory.path());
		QVERIFY(!item.src.isEmpty());
		item.bytes = QFileInfo(item.src).size();
		item.headerIdentity = OperationHeaderIdentity{{}, {}, QStringLiteral("Recorded rescan instruction.")};
		QString error;
		auto source = OperationFile::open(item.src, false, error);
		QVERIFY2(source, qPrintable(error));
		const auto stamp = source->stamp();
		QVERIFY(!FileCheck::sourceMatches(item, *source, stamp, stamp, error));
		QCOMPARE(error, item.headerIdentity->unavailableReason);
	}
};

QTEST_GUILESS_MAIN(TestFileCheck)
#include "tst_file_check.moc"
