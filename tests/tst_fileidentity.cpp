#include "fileidentity.h"
#include "mxfparser.h"
#include "nativefile.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

// FileIdentity / VolumeIdentity / NativeFile — the engine v2 identity
// and platform primitives, driven over real temp files (which sit on a
// proven-local volume on both CI platforms: APFS on macOS, NTFS on
// Windows) plus the fixture MXF headers for the content-UMID half.

namespace
{

	QByteArray readFixture(const char *relative)
	{
		QFile f(QStringLiteral(FIXTURES_DIR "/") + QLatin1String(relative));
		if (!f.open(QIODevice::ReadOnly))
			return {};
		return f.readAll();
	}
} // namespace

class TestFileIdentity : public QObject
{
	Q_OBJECT
private slots:
	// MARK: - FileIdentity, filesystem half
	void capture_regular_file_is_full_strength_on_local_volume();
	void capture_missing_file_has_no_strength();
	void verify_unchanged_file_matches();
	void verify_size_change_is_changed();
	void verify_swapped_same_size_file_is_changed_on_full();
	void verify_in_place_edit_same_size_matches_on_full();
	void verify_missing_file_is_missing();
	void sizetime_tier_uses_mtime();

	// MARK: - FileIdentity, content half
	void capture_fixture_mxf_reads_umid();
	void verify_media_swap_same_object_is_changed_by_umid();

	// MARK: - FileIdentity, journal round-trip
	void identity_json_round_trip_preserves_large_ids();

	// MARK: - VolumeIdentity
	void volume_capture_local_has_full_identity();
	void volume_capture_twice_matches();
	void volume_weak_fingerprints_compare_by_triple();
	void volume_none_never_matches();
	void volume_json_round_trip();

	// MARK: - NativeFile
	void proven_local_accepts_temp_dir();
	void proven_local_rejects_empty_and_missing();
	void sync_file_holds_both_barriers_on_local_disk();
	void sync_directory_succeeds_on_local_disk();
	void clone_behaviour_matches_platform();
	void win_copy_behaviour_matches_platform();
};

// MARK: - FileIdentity, filesystem half

void TestFileIdentity::capture_regular_file_is_full_strength_on_local_volume()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(1234, 'x'));
	QVERIFY(!path.isEmpty());

	const FileIdentity id = FileIdentity::capture(path);
	QCOMPARE(id.size, qint64(1234));
	QVERIFY(id.mtimeNs != 0);
	// Temp dirs on the CI platforms sit on APFS/NTFS — allowlisted, so
	// the disk's own file ID must have been captured and trusted.
	QCOMPARE(id.confidence, FileIdentity::Confidence::High);
	QVERIFY(id.fileId != 0);
	// Not an .mxf, so the content half must be honestly absent.
	QVERIFY(id.contentUmid.isEmpty());
}

void TestFileIdentity::capture_missing_file_has_no_strength()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const FileIdentity id = FileIdentity::capture(tmp.path() + QStringLiteral("/nope.bin"));
	QCOMPARE(id.confidence, FileIdentity::Confidence::Low);
	QCOMPARE(id.size, qint64(-1));
}

void TestFileIdentity::verify_unchanged_file_matches()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(64, 'x'));
	const FileIdentity id = FileIdentity::capture(path);
	QCOMPARE(FileIdentity::verify(path, id), FileIdentity::Verdict::Match);
}

void TestFileIdentity::verify_size_change_is_changed()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(64, 'x'));
	const FileIdentity id = FileIdentity::capture(path);

	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::Append));
		f.write("more");
	}

	FileIdentity actual;
	QCOMPARE(FileIdentity::verify(path, id, &actual), FileIdentity::Verdict::Changed);
	// The explanation names the first difference in plain words.
	QVERIFY(FileIdentity::explainDifference(id, actual).contains(QStringLiteral("size")));
}

void TestFileIdentity::verify_swapped_same_size_file_is_changed_on_full()
{
	// The dialog-left-open scenario: the selected file is deleted and a
	// DIFFERENT file of the same size appears at the same path. Size and
	// name agree; only the disk's file ID knows. On a proven-local
	// volume that must be enough to refuse.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(64, 'x'));
	const QString other = writeFileIn(tmp.path(), QStringLiteral("b.bin"), QByteArray(64, 'y'));
	const FileIdentity id = FileIdentity::capture(path);
	QCOMPARE(id.confidence, FileIdentity::Confidence::High);

	QVERIFY(QFile::remove(path));
	QVERIFY(QFile::rename(other, path));

	FileIdentity actual;
	QCOMPARE(FileIdentity::verify(path, id, &actual), FileIdentity::Verdict::Changed);
	QVERIFY(FileIdentity::explainDifference(id, actual)
				.contains(QStringLiteral("different file")));
}

void TestFileIdentity::verify_in_place_edit_same_size_matches_on_full()
{
	// Documented subtlety, pinned on purpose: an in-place rewrite that
	// keeps the size keeps the file ID too — same file OBJECT, so the
	// Full tier says Match (mtime is informational there). Media swaps
	// are the content half's job, not the filesystem half's.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(64, 'x'));
	const FileIdentity id = FileIdentity::capture(path);
	QCOMPARE(id.confidence, FileIdentity::Confidence::High);

	QThread::msleep(15); // ensure the mtime actually moves
	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly)); // truncates, same inode
		f.write(QByteArray(64, 'z'));
	}

	QCOMPARE(FileIdentity::verify(path, id), FileIdentity::Verdict::Match);
}

void TestFileIdentity::verify_missing_file_is_missing()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(8, 'x'));
	const FileIdentity id = FileIdentity::capture(path);
	QVERIFY(QFile::remove(path));
	QCOMPARE(FileIdentity::verify(path, id), FileIdentity::Verdict::Missing);
}

void TestFileIdentity::sizetime_tier_uses_mtime()
{
	// On a weak volume only size+mtime carry the filesystem half. Force
	// the tier by hand (the temp dir is really Full-confidence) and check
	// an mtime change alone flips the verdict.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("a.bin"), QByteArray(64, 'x'));
	FileIdentity id = FileIdentity::capture(path);
	id.confidence = FileIdentity::Confidence::Med;

	QThread::msleep(15);
	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(QByteArray(64, 'z')); // same size, new mtime
	}

	QCOMPARE(FileIdentity::verify(path, id), FileIdentity::Verdict::Changed);
}

// MARK: - FileIdentity, content half

void TestFileIdentity::capture_fixture_mxf_reads_umid()
{
	const QString fixture =
		QStringLiteral(FIXTURES_DIR "/avid_headers/V01.E683C412_F82F4F82F461DV.mxf");
	QVERIFY(QFile::exists(fixture));

	const FileIdentity id = FileIdentity::capture(fixture);
	QVERIFY(!id.contentUmid.isEmpty());
	// Same parser, same answer: the identity's UMID is exactly what the
	// scanner would report for this file.
	QCOMPARE(id.contentUmid, MxfParser::parseHeader(fixture).umid);
}

void TestFileIdentity::verify_media_swap_same_object_is_changed_by_umid()
{
	// The hardest swap: the file is rewritten IN PLACE (same file ID)
	// with different media padded/cut to the same byte size. Every
	// filesystem check passes; only the UMID inside the header knows.
	//
	// The two fixtures must come from DIFFERENT clips: the header UMID is
	// the MASTER clip's identity, shared by all of one clip's files (its
	// V01 and A01 carry the same UMID — a first draft of this test
	// learned that the hard way).
	const QByteArray headerA = readFixture("avid_headers/V01.E683C412_F82F4F82F461DV.mxf");
	QByteArray headerB = readFixture("avid_headers/V01.E683CD72_FF4BEFF4BE92DV.mxf");
	QVERIFY(!headerA.isEmpty());
	QVERIFY(!headerB.isEmpty());

	// Cut/pad B to exactly A's size so size can't be the tell.
	headerB.resize(headerA.size());

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeFileIn(tmp.path(), QStringLiteral("clip.mxf"), headerA);
	const FileIdentity id = FileIdentity::capture(path);
	QVERIFY(!id.contentUmid.isEmpty());

	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly)); // in place: same file ID
		f.write(headerB);
	}

	QCOMPARE(FileIdentity::verify(path, id), FileIdentity::Verdict::Changed);
}

// MARK: - FileIdentity, journal round-trip

void TestFileIdentity::identity_json_round_trip_preserves_large_ids()
{
	FileIdentity id;
	id.size = Q_INT64_C(5'368'709'120); // a 5 GB MXF
	id.mtimeNs = Q_INT64_C(1'756'400'000'123'456'789);
	// Top-bit-set 64-bit values are exactly what JSON numbers mangle —
	// the hex-string encoding must bring them back untouched.
	id.fileId = Q_UINT64_C(0xFFFFFFFFFFFFFFFF);
	id.volumeId = Q_UINT64_C(0x8000000000000001);
	id.contentUmid = QStringLiteral("060A2B340101010101010F0013000000");
	id.confidence = FileIdentity::Confidence::High;

	const FileIdentity back = FileIdentity::fromJson(id.toJson());
	QCOMPARE(back.size, id.size);
	QCOMPARE(back.mtimeNs, id.mtimeNs);
	QCOMPARE(back.fileId, id.fileId);
	QCOMPARE(back.volumeId, id.volumeId);
	QCOMPARE(back.contentUmid, id.contentUmid);
	QCOMPARE(back.confidence, id.confidence);
}

// MARK: - VolumeIdentity

void TestFileIdentity::volume_capture_local_has_full_identity()
{
	const VolumeIdentity v = VolumeIdentity::capture(QDir::tempPath());
	// CI temp volumes are APFS / local NTFS: the OS mints them a real
	// identity, and capture must have found it.
	QCOMPARE(v.confidence, VolumeIdentity::Confidence::High);
	QVERIFY(!v.rootPath.isEmpty());
	QVERIFY(!v.fsType.isEmpty());
	QVERIFY(v.capacityBytes > 0);
	QVERIFY(!v.uuid.isEmpty() || v.serial != 0);
}

void TestFileIdentity::volume_capture_twice_matches()
{
	const VolumeIdentity a = VolumeIdentity::capture(QDir::tempPath());
	const VolumeIdentity b = VolumeIdentity::capture(QDir::tempPath());
	QVERIFY(a.matches(b));
	QVERIFY(b.matches(a));
}

void TestFileIdentity::volume_weak_fingerprints_compare_by_triple()
{
	VolumeIdentity a;
	a.label = QStringLiteral("NEXIS_WS1");
	a.fsType = QStringLiteral("smbfs");
	a.capacityBytes = Q_INT64_C(64'000'000'000'000);
	a.confidence = VolumeIdentity::Confidence::Med;

	VolumeIdentity same = a;
	QVERIFY(a.matches(same));

	VolumeIdentity differentSize = a;
	differentSize.capacityBytes += 1;
	QVERIFY(!a.matches(differentSize));

	VolumeIdentity differentLabel = a;
	differentLabel.label = QStringLiteral("NEXIS_WS2");
	QVERIFY(!a.matches(differentLabel));
}

void TestFileIdentity::volume_none_never_matches()
{
	const VolumeIdentity none =
		VolumeIdentity::capture(QStringLiteral("/definitely/not/a/mount/zzz"));
	QCOMPARE(none.confidence, VolumeIdentity::Confidence::Low);
	// "Can't tell" must read as "don't touch", even against itself.
	QVERIFY(!none.matches(none));
}

void TestFileIdentity::volume_json_round_trip()
{
	VolumeIdentity v;
	v.uuid = QStringLiteral("8F2E9B7A-1C3D-4E5F-A6B7-C8D9E0F1A2B3");
	v.serial = 0xDEADBEEF;
	v.label = QStringLiteral("EDIT 1");
	v.fsType = QStringLiteral("apfs");
	v.capacityBytes = Q_INT64_C(4'000'000'000'000);
	v.rootPath = QStringLiteral("/Volumes/EDIT 1");
	v.confidence = VolumeIdentity::Confidence::High;

	const VolumeIdentity back = VolumeIdentity::fromJson(v.toJson());
	QCOMPARE(back.uuid, v.uuid);
	QCOMPARE(back.serial, v.serial);
	QCOMPARE(back.label, v.label);
	QCOMPARE(back.fsType, v.fsType);
	QCOMPARE(back.capacityBytes, v.capacityBytes);
	QCOMPARE(back.rootPath, v.rootPath);
	QCOMPARE(back.confidence, v.confidence);
	QVERIFY(v.matches(back));
}

// MARK: - NativeFile

void TestFileIdentity::proven_local_accepts_temp_dir()
{
	// APFS on macOS CI, NTFS on Windows CI — both allowlisted.
	QVERIFY(NativeFile::isProvenLocalVolume(QDir::tempPath()));
}

void TestFileIdentity::proven_local_rejects_empty_and_missing()
{
	QVERIFY(!NativeFile::isProvenLocalVolume(QString()));
	QVERIFY(!NativeFile::isProvenLocalVolume(QStringLiteral("/definitely/not/a/mount/zzz")));
#ifdef Q_OS_WIN
	// The UNC prefix must be rejected before any filesystem-name lookup:
	// an SMB share reports the SERVER'S filesystem ("NTFS").
	QVERIFY(!NativeFile::isProvenLocalVolume(QStringLiteral("\\\\server\\share\\file.mxf")));
	QVERIFY(!NativeFile::isProvenLocalVolume(QStringLiteral("//server/share/file.mxf")));
#endif
}

void TestFileIdentity::sync_file_holds_both_barriers_on_local_disk()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QFile f(tmp.path() + QStringLiteral("/sync.bin"));
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(QByteArray(4096, 'd'));

	QCOMPARE(NativeFile::syncFile(f, NativeFile::Durability::Disk), NativeFile::SyncResult::Ok);
	// A local APFS/NTFS volume supports the full barrier — no degrade.
	QCOMPARE(NativeFile::syncFile(f, NativeFile::Durability::Platter), NativeFile::SyncResult::Ok);
}

void TestFileIdentity::sync_directory_succeeds_on_local_disk()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// This must hold on BOTH platforms now — the Windows directory-flush
	// was the v1 journal's knowingly missing piece.
	QVERIFY(NativeFile::syncDirectory(tmp.path()));
}

void TestFileIdentity::clone_behaviour_matches_platform()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = writeFileIn(tmp.path(), QStringLiteral("src.bin"), QByteArray(8192, 'c'));
	const QString dst = tmp.path() + QStringLiteral("/dst.bin");

#ifdef Q_OS_MAC
	// Same APFS volume: the clone must land, byte-identical.
	QVERIFY(NativeFile::clone(src, dst));
	QFile a(src), b(dst);
	QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
	QCOMPARE(b.readAll(), a.readAll());

	// clonefile refuses an existing destination — the guarantee the
	// park-aside dance relies on.
	QVERIFY(!NativeFile::clone(src, dst));

	// The test seam forces the buffered path.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
	const QString dst2 = tmp.path() + QStringLiteral("/dst2.bin");
	QVERIFY(!NativeFile::clone(src, dst2));
	qunsetenv("MEDIAMUSTER_DISABLE_CLONEFILE");
	qunsetenv("MEDIAMUSTER_DISABLE_COPYFILEEX");
#else
	// Not a Mac: clone is never available and must say so quietly.
	QVERIFY(!NativeFile::clone(src, dst));
#endif
}

void TestFileIdentity::win_copy_behaviour_matches_platform()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = writeFileIn(tmp.path(), QStringLiteral("src.bin"), QByteArray(65536, 'w'));
	const QString dst = tmp.path() + QStringLiteral("/dst.bin");
	std::atomic<bool> cancel{false};

#ifdef Q_OS_WIN
	bool progressed = false;
	const auto outcome = NativeFile::copyWin(
		src, dst,
		[&progressed](qint64, qint64) { progressed = true; }, cancel);
	QCOMPARE(outcome, NativeFile::WinCopyOutcome::Succeeded);
	QVERIFY(progressed);
	{
		QFile a(src), b(dst);
		QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
		QCOMPARE(b.readAll(), a.readAll());
	}

	// FAIL_IF_EXISTS: the no-overwrite guarantee after parking. A
	// pre-existing destination is the distinct racer outcome (review
	// finding 7) — NOT a plain failure, and not ours to delete. errorOut
	// stays untouched on this path; the copier composes its own message.
	QString err;
	QCOMPARE(NativeFile::copyWin(src, dst, {}, cancel, &err),
			 NativeFile::WinCopyOutcome::RefusedExists);
	QVERIFY(err.isEmpty());

	// A cancel arriving before the first chunk aborts the copy AND the
	// OS deletes its own partial — nothing survives to masquerade as
	// media.
	cancel.store(true);
	const QString dst2 = tmp.path() + QStringLiteral("/dst2.bin");
	QCOMPARE(NativeFile::copyWin(src, dst2, {}, cancel),
			 NativeFile::WinCopyOutcome::Cancelled);
	QVERIFY(!QFile::exists(dst2));
#else
	// Not Windows: the primitive must report Unavailable so the caller
	// falls through to the engine's own loop.
	QCOMPARE(NativeFile::copyWin(src, dst, {}, cancel),
			 NativeFile::WinCopyOutcome::Unavailable);
#endif
}

QTEST_APPLESS_MAIN(TestFileIdentity)
#include "tst_fileidentity.moc"
