#include "volumeidentity.h"
#include "nativefile.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class TestVolumeIdentity : public QObject
{
	Q_OBJECT
  private slots:
	void local_volume_round_trip();
	void a_different_volume_never_matches();
	void labels_and_capacity_are_not_identity();
	void local_durability_requests();
	void directory_sync_reports_native_error();
	void unsupported_directory_flush_is_not_an_io_failure();
};
void TestVolumeIdentity::local_volume_round_trip()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const auto id = VolumeIdentity::capture(dir.path());
	QCOMPARE(id.confidence, VolumeIdentity::Confidence::High);
	QVERIFY(id.matches(VolumeIdentity::capture(dir.path())));
	QVERIFY(id.matches(VolumeIdentity::fromJson(id.toJson())));
}
void TestVolumeIdentity::a_different_volume_never_matches()
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
void TestVolumeIdentity::labels_and_capacity_are_not_identity()
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
void TestVolumeIdentity::local_durability_requests()
{
	QTemporaryDir dir;
	QFile file(dir.path() + "/data");
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
	QCOMPARE(file.write("test", 4), 4);
	QCOMPARE(NativeFile::syncFile(file),
			 NativeFile::SyncResult::Ok);
	QCOMPARE(NativeFile::syncDirectory(dir.path()), NativeFile::SyncResult::Ok);
}
void TestVolumeIdentity::directory_sync_reports_native_error()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString missing = dir.path() + "/missing/child";
	QString error;
	QCOMPARE(NativeFile::syncDirectory(missing, &error), NativeFile::SyncResult::Failed);
	QVERIFY2(error.contains(missing), qPrintable(error));
#ifdef Q_OS_WIN
	QVERIFY2(error.contains("CreateFileW(directory)"), qPrintable(error));
	QVERIFY2(error.contains("Windows error 3"), qPrintable(error)); // ERROR_PATH_NOT_FOUND
#else
	QVERIFY2(error.contains("open(directory)"), qPrintable(error));
	QVERIFY2(error.contains("POSIX error 2:"), qPrintable(error)); // ENOENT
#endif
	QCOMPARE(NativeFile::syncDirectory(dir.path(), &error), NativeFile::SyncResult::Ok);
	QVERIFY(error.isEmpty());
}
void TestVolumeIdentity::unsupported_directory_flush_is_not_an_io_failure()
{
	using Sync = NativeFile::SyncResult;
	QCOMPARE(NativeFile::directoryFlushResult(0), Sync::Ok);
#ifdef Q_OS_WIN
	for (const int code : {ERROR_INVALID_FUNCTION, ERROR_NOT_SUPPORTED})
		QCOMPARE(NativeFile::directoryFlushResult(code), Sync::OkDegraded);
	for (const int code : {ERROR_ACCESS_DENIED, ERROR_INVALID_HANDLE, ERROR_NOT_READY,
						   ERROR_WRITE_FAULT, ERROR_IO_DEVICE, ERROR_INVALID_PARAMETER})
		QCOMPARE(NativeFile::directoryFlushResult(code), Sync::Failed);
#else
	QCOMPARE(NativeFile::directoryFlushResult(ENOTSUP), Sync::OkDegraded);
	for (const int code : {EIO, EACCES, EINVAL, ENOENT})
		QCOMPARE(NativeFile::directoryFlushResult(code), Sync::Failed);
#endif
}
QTEST_GUILESS_MAIN(TestVolumeIdentity)
#include "tst_volumeidentity.moc"
