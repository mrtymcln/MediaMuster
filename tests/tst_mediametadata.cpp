// Recorded import paths use the producer's separators, independent of this host.

#include "mediametadata.h"
#include <QTest>

class TestMediaMetadata : public QObject
{
	Q_OBJECT
private slots:
	void source_basename_data();
	void source_basename();
};

void TestMediaMetadata::source_basename_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<QString>("expected");
	QTest::newRow("mac-path") << QStringLiteral("/Volumes/EDIT/Rushes/clip.mov") << QStringLiteral("clip.mov");
	QTest::newRow("windows-path") << QStringLiteral("C:\\Rushes\\clip.mov") << QStringLiteral("clip.mov");
	QTest::newRow("network-path") << QStringLiteral("\\\\NEXIS\\Workspace\\clip.mov") << QStringLiteral("clip.mov");
	QTest::newRow("mixed-separators") << QStringLiteral("C:\\Rushes/day1/clip.mov") << QStringLiteral("clip.mov");
	QTest::newRow("filename-only") << QStringLiteral("Nön English 你好.mov") << QStringLiteral("Nön English 你好.mov");
	QTest::newRow("empty") << QString() << QString();
	QTest::newRow("trailing-separator") << QStringLiteral("/Rushes/") << QString();
}

void TestMediaMetadata::source_basename()
{
	QFETCH(QString, path);
	QFETCH(QString, expected);
	QCOMPARE(MediaMetadataUtil::sourceFileBaseName(path), expected);
}

QTEST_APPLESS_MAIN(TestMediaMetadata)
#include "tst_mediametadata.moc"
