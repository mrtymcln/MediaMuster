#include "fileidentity.h"
#include "nativefile.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>

class TestFileIdentity : public QObject
{
	Q_OBJECT
  private slots:
	void local_volume_round_trip();
	void a_different_volume_never_matches();
	void labels_and_capacity_are_not_identity();
	void local_durability_requests();
};
void TestFileIdentity::local_volume_round_trip()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const auto id = VolumeIdentity::capture(dir.path());
	QCOMPARE(id.confidence, VolumeIdentity::Confidence::High);
	QVERIFY(id.matches(VolumeIdentity::capture(dir.path())));
	QVERIFY(id.matches(VolumeIdentity::fromJson(id.toJson())));
}
void TestFileIdentity::a_different_volume_never_matches()
{
	QTemporaryDir dir;
	const auto id = VolumeIdentity::capture(dir.path());
	auto other = id;
	other.uuid = "another-volume";
	QVERIFY(!id.matches(other));
	other = id;
	other.serial = id.serial + 1;
	if (id.serial)
		QVERIFY(!id.matches(other));
}
void TestFileIdentity::labels_and_capacity_are_not_identity()
{
	VolumeIdentity a;
	a.confidence = VolumeIdentity::Confidence::Med;
	a.label = "MEDIA";
	a.fsType = "network";
	a.capacityBytes = 1000000;
	auto b = a;
	QVERIFY(!a.matches(b));
	a.confidence = VolumeIdentity::Confidence::High;
	b.confidence = a.confidence;
	QVERIFY(!a.matches(b)); // Missing native IDs do not become strong by label.
}
void TestFileIdentity::local_durability_requests()
{
	QTemporaryDir dir;
	QFile file(dir.path() + "/data");
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
	QCOMPARE(file.write("test", 4), 4);
	QCOMPARE(NativeFile::syncFile(file, NativeFile::Durability::Platter),
			 NativeFile::SyncResult::Ok);
	QVERIFY(NativeFile::syncDirectory(dir.path()));
}
QTEST_GUILESS_MAIN(TestFileIdentity)
#include "tst_fileidentity.moc"
