#include "oprunner.h"
#include "operationplan.h"
#include "operationrecovery.h"
#include "opdiagnostics.h"
#include "optrash.h"
#include "mxfparser.h"
#include <QJsonArray>
#include <QTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFileInfo>
#include <QProcess>
#include <QCryptographicHash>
#include <cstdlib>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <stdexcept>
#include <limits>
#ifdef Q_OS_MAC
#include <sys/mount.h>
#endif
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace
{
void put(const QString &path, const QByteArray &bytes)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
		throw std::runtime_error("mkdir fixture");
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
		throw std::runtime_error("write fixture");
}
QByteArray get(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return f.readAll();
}
struct Sink : OpSink
{
	QVector<OpResult> results;
	QStringList messages;
	void progress(const QString &, int, int, double) override {}
	void log(QtMsgType, const QString &s) override
	{
		messages.append(s);
	}
	void trashUsed(const QString &, int) override {}
	void result(const OpResult &r) override
	{
		results.append(r);
	}
};
struct Fixture
{
	QTemporaryDir dir;
	QString root = OpJournal::canonicalPath(dir.path());
	QString src = root + "/source/clip.bin", dest = root + "/destination",
			journals = root + "/journals";
	QByteArray bytes = QByteArray(8 * 1024 * 1024, 'x');
	Fixture()
	{
		put(src, bytes);
		QDir().mkpath(dest);
	}
	OpRequest request(OpKind kind = OpKind::Copy, QString policy = {}) const
	{
		OpRequest r;
		r.kind = kind;
		r.verifyCopies = true;
		r.diagnosticTrashRoot = root + "/_MediaMuster_Trash";
		r.destRoot = dest;
		OpItem i;
		i.src = src;
		i.name = "clip.bin";
		i.bytes = bytes.size();
		i.policy = policy;
		r.items.append(i);
		return r;
	}
};
#ifdef Q_OS_WIN
constexpr int transientCopyError = ERROR_NETWORK_BUSY;
constexpr int permanentCopyError = ERROR_ACCESS_DENIED;
#else
constexpr int transientCopyError = EBUSY;
constexpr int permanentCopyError = EACCES;
#endif
} // namespace
class TestFileOperations : public QObject
{
	Q_OBJECT
  private slots:
	void copy_verifies_and_publishes();
	void advisory_copy_move_assessment_data();
	void advisory_copy_move_assessment();
	void advisory_destination_paths();
	void advisory_space_estimate_saturates();
	void move_rechecks_advisory_strategy();
	void already_at_destination_move_data();
	void already_at_destination_move();
	void already_at_destination_copy_is_not_undone();
	void already_at_destination_is_rechecked();
	void already_at_destination_only_job();
	void undo_already_restored_object_finishes_without_changes();
	void resume_flush_failure_stays_unfinished();
	void undo_late_collision_remains_resumable();
	void resume_continues_past_failed_source();

	void network_delete_always_uses_mediamuster_trash();
	void retry_is_bounded_and_journalled();
	void native_copy_error_classification_data();
	void native_copy_error_classification();
	void native_copy_retry_policy_data();
	void native_copy_retry_policy();
	void native_copy_retry_cancellation_data();
	void native_copy_retry_cancellation();
	void native_copy_retry_stops_on_journal_failure();
	void publication_failure_does_not_recopy();
	void undo_rebalance_retires_regenerated_indexes();
	void undo_original_in_retirement();

	void verification_off_avoids_readback();
	void move_copies_every_file_before_removal();
	void failed_copy_continues_and_blocks_all_original_removal();
	void explicit_skip_survives_disappearing_conflict();
	void removal_crash_boundaries_resume();
	void undo_copy_is_gated_and_claims_forward();
	void undo_partial_move_restores_only_completed_changes();
	void undo_interrupted_copy_and_changed_result();
	void undo_move_resume_keeps_saved_policy();
	void local_trash_and_undo_roundtrip();

	void keep_both_retains_names_and_handles_late_race();
	void skip_never_overwrites();
	void replace_is_rejected();
	void source_edit_is_retained();
	void cancellation_preserves_replacement();
	void corrupt_readback_never_publishes();
	void failed_cleanup_remains_journalled();
	void journal_failure_stops_publication();
	void missing_journal_prevents_relocation();
	void crash_before_verification_is_not_complete();
	void crash_after_publication_is_reconciled();
	void repeated_resume_continues_same_journal();
	void source_retention_is_explicit();
	void same_volume_move_preserves_identity();
	void rename_group_conflict_skips_every_member();
	void rename_failure_stops_group();
	void different_source_on_resume_is_refused();
	void corrupt_journal_is_preserved();
	void trash_preserves_original_and_location();
	void partial_group_failure_keeps_every_file_recorded();
	void same_volume_keep_both_handles_late_conflict();
	void real_mxf_identity_is_checked();
	void mxf_parser_borrows_protected_handle();
	void journal_volume_paths_survive_two_resolutions();
	void mismatched_volume_is_never_session_matched();
	void second_runner_cannot_change_files();
	void debug_harness_uses_disposable_files();
	void debug_setup_failure_saves_report();
	void unsupported_directory_flush_preserves_originals();
	void directory_io_failure_stops_copy();
	void unsupported_relocation_keeps_media_and_databases();
	void directory_creation_propagates_durability();
	void debug_harness_runs_copies_without_directory_flush();
	void bundled_samples_match_supplied_files();
	void selected_duplicates_keep_both();
	void regenerated_database_is_retired_on_resume();
	void torn_tail_retains_verified_prefix();
	void abrupt_process_exit_is_recoverable();
#ifdef Q_OS_WIN
	void encrypted_windows_copy_is_refused();
#endif
	void rebalance_late_conflict_stops_remaining_members();
};
void TestFileOperations::copy_verifies_and_publishes()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	auto totals = runner.run(f.request(), f.journals);
	QVERIFY2(totals.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.src), f.bytes);
	const auto records = OpJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records[0].entries[0].step, OpJournal::Step::Done);
	QVERIFY(!records[0].entries[0].hash.isEmpty());
	QVERIFY(records[0].entries[0].landed.valid());
	QCOMPARE(records[0].entries[0].originalSource, f.src);
}
void TestFileOperations::advisory_copy_move_assessment_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<bool>("secondCanRelocate");
	QTest::addColumn<bool>("skipSecond");
	QTest::addColumn<bool>("forceCopy");
	QTest::addColumn<bool>("copyThenRemove");
	QTest::addColumn<qint64>("temporaryBytes");
	QTest::newRow("copy") << int(OpKind::Copy) << true << false << false << false << qint64(300);
	QTest::newRow("same-volume-move") << int(OpKind::Move) << true << false << false << false << qint64(0);
	QTest::newRow("mixed-volume-move") << int(OpKind::Move) << false << false << false << true << qint64(300);
	QTest::newRow("skip-cross-volume") << int(OpKind::Move) << false << true << false << false << qint64(0);
	QTest::newRow("copy-excludes-skip") << int(OpKind::Copy) << false << true << false << false << qint64(100);
	QTest::newRow("forced-copy-move") << int(OpKind::Move) << true << false << true << true << qint64(300);
	QTest::newRow("delete") << int(OpKind::Delete) << false << false << false << false << qint64(0);
}
void TestFileOperations::advisory_copy_move_assessment()
{
	QFETCH(int, kind);
	QFETCH(bool, secondCanRelocate);
	QFETCH(bool, skipSecond);
	QFETCH(bool, forceCopy);
	QFETCH(bool, copyThenRemove);
	QFETCH(qint64, temporaryBytes);
	OpRequest request;
	request.kind = static_cast<OpKind>(kind);
	request.destRoot = "/destination";
	OpItem first;
	first.src = "/source/first.mxf";
	first.name = "first.mxf";
	first.bytes = 100;
	auto second = first;
	second.src = "/other/second.mxf";
	second.name = "second.mxf";
	second.bytes = 200;
	if (skipSecond) second.policy = "skip";
	request.items = {first, second};
	QStringList probedSources, probedDestinations;
	const auto assessment = OperationPlan::assessCopyMove(request,
		[&](const QString &source, const QString &destination) {
			probedSources.append(source);
			probedDestinations.append(destination);
			return source == first.src || secondCanRelocate;
		}, forceCopy);
	QCOMPARE(assessment.copyThenRemove, copyThenRemove);
	QCOMPARE(assessment.temporaryBytes, temporaryBytes);
	if (request.kind == OpKind::Move)
	{
		QCOMPARE(probedSources.size(), skipSecond ? 1 : 2);
		QCOMPARE(probedDestinations[0], QString("/destination/first.mxf"));
		if (skipSecond) QVERIFY(!probedSources.contains(second.src));
	}
	else
		QVERIFY(probedSources.isEmpty());
}
void TestFileOperations::advisory_destination_paths()
{
	QCOMPARE(OperationPlan::destinationPath("clip.mxf", "7", "/media", false),
		QString("/media/clip.mxf"));
	QCOMPARE(OperationPlan::destinationPath("clip.mxf", "7", "/media", true),
		QString("/media/Avid MediaFiles/MXF/7/clip.mxf"));
	QCOMPARE(OperationPlan::destinationPath("clip.omf", "7", "/media", true, true),
		QString("/media/OMFI MediaFiles/clip.omf"));
	Fixture f;
	put(f.dest + "/clip (2).bin", "occupied");
	const auto candidate = OperationPlan::findKeepBothPath(f.dest + "/clip.bin");
	QVERIFY(candidate);
	QCOMPARE(*candidate, f.dest + "/clip (3).bin");
	QVERIFY(!QFile::exists(*candidate)); // Advisory naming never creates/reserves a file.
}
void TestFileOperations::advisory_space_estimate_saturates()
{
	OpRequest request;
	OpItem item;
	item.bytes = (std::numeric_limits<qint64>::max)();
	request.items = {item, item};
	QCOMPARE(OperationPlan::assessCopyMove(request).temporaryBytes, item.bytes);
}
void TestFileOperations::move_rechecks_advisory_strategy()
{
	Fixture f;
	auto request = f.request(OpKind::Move);
	QVERIFY(!OperationPlan::assessCopyMove(request).copyThenRemove);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true; // Execution has stricter capability evidence than the preview.
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto saved = OpJournal::scan(f.journals);
	QCOMPARE(saved.size(), 1);
	QVERIFY(saved[0].request.copyThenRemove);
	QCOMPARE(saved[0].entries[0].attempts, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QVERIFY(!QFile::exists(f.src));
}
void TestFileOperations::already_at_destination_move_data()
{
	QTest::addColumn<bool>("resume");
	QTest::newRow("uninterrupted") << false;
	QTest::newRow("resume-after-no-effect") << true;
}
void TestFileOperations::already_at_destination_move()
{
	QFETCH(bool, resume);
	Fixture f;
	const QString existing = f.dest + "/already.bin";
	const QByteArray existingBytes("pre-existing destination");
	put(existing, existingBytes);
	const auto existingStamp = OpFile::inspect(existing);
	auto request = f.request(OpKind::Move);
	auto noEffect = request.items[0];
	noEffect.src = existing;
	noEffect.name = "already.bin";
	noEffect.bytes = existingBytes.size();
	request.items.prepend(noEffect);
	const auto assessment = OperationPlan::assessCopyMove(request,
		[](const QString &, const QString &) { return false; });
	QVERIFY(assessment.copyThenRemove);
	QCOMPARE(assessment.temporaryBytes, qint64(f.bytes.size()));
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true; // Exercise the mixed-volume Move strategy on disposable local files.
	if (resume)
		runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
			if (stage == "no-effect") cancel = true;
		};
	auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.unchanged, 1);
	QCOMPARE(totals.skipped, 0);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.needsAttention, 0);
	auto records = OpJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const QString forwardPath = records[0].path;
	const auto &savedNoEffect = records[0].entries[0];
	QCOMPARE(savedNoEffect.step, OpJournal::Step::NoEffect);
	QVERIFY(savedNoEffect.complete());
	QVERIFY(!savedNoEffect.explicitSkip);
	QVERIFY(!savedNoEffect.sourceRemoved);
	QVERIFY(savedNoEffect.mechanism.isEmpty());
	QVERIFY(savedNoEffect.source.unchanged(savedNoEffect.landed));
	QCOMPARE(savedNoEffect.attempts, 0);
	QCOMPARE(sink.results[0].state, OpResult::State::NoEffect);
	if (resume)
	{
		QVERIFY(totals.cancelled);
		QCOMPARE(records[0].entries[1].attempts, 0);
		QCOMPARE(get(f.src), f.bytes);
		cancel = false;
		runner.hooks = {};
		OpRequest continuation;
		continuation.resumeJournalPath = forwardPath;
		totals = runner.run(continuation, f.journals);
	}
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.retained, 0);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.needsAttention, 0);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QVERIFY(existingStamp.unchanged(OpFile::inspect(existing)));
	const auto forward = OpJournal::readOne(forwardPath);
	QVERIFY(forward);
	QVERIFY(forward->copiesComplete);
	QCOMPARE(forward->entries[0].step, OpJournal::Step::NoEffect);
	QCOMPARE(forward->entries[1].step, OpJournal::Step::SourceRemoved);
	QVERIFY(OperationRecovery::pending(f.journals).isEmpty());
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forwardPath;
	const auto undone = runner.run(undo, f.journals);
	QCOMPARE(undone.succeeded, 1);
	QCOMPARE(undone.needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(existingStamp.unchanged(OpFile::inspect(existing)));
}
void TestFileOperations::already_at_destination_copy_is_not_undone()
{
	Fixture f;
	const QString existing = f.dest + "/already.bin";
	put(existing, "already here");
	const auto before = OpFile::inspect(existing);
	auto request = f.request();
	auto noEffect = request.items[0];
	noEffect.src = existing;
	noEffect.name = "already.bin";
	noEffect.bytes = before.size;
	request.items.prepend(noEffect);
	QCOMPARE(OperationPlan::assessCopyMove(request).temporaryBytes, qint64(f.bytes.size()));
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.unchanged, 1);
	const auto forward = OpJournal::scan(f.journals)[0];
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	QCOMPARE(runner.run(undo, f.journals).succeeded, 1);
	QVERIFY(before.unchanged(OpFile::inspect(existing)));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	for (const auto &record : OpJournal::scan(f.journals))
		if (record.request.kind == OpKind::Undo)
		{
			QCOMPARE(record.entries.size(), 1);
			QCOMPARE(record.entries[0].undoEntryId, 1);
		}
}
void TestFileOperations::already_at_destination_is_rechecked()
{
	Fixture f;
	auto request = f.request();
	// A stale advisory answer cannot authorize treating a different live file as a no-op.
	const auto preview = OperationPlan::assessCopyMove(request,
		OperationPlan::sameVolumeForRename, false,
		[](const QString &, const QString &) { return true; });
	QCOMPARE(preview.temporaryBytes, qint64(0));
	put(f.dest + "/clip.bin", "a different existing file");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(totals.unchanged, 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("a different existing file"));
}
void TestFileOperations::already_at_destination_only_job()
{
	for (const auto kind : {OpKind::Copy, OpKind::Move})
	{
		Fixture f;
		auto request = f.request(kind);
		request.destRoot = QFileInfo(f.src).absolutePath();
		QVERIFY(OperationPlan::alreadyAtDestination(f.src, f.src));
		QCOMPARE(OperationPlan::assessCopyMove(request).temporaryBytes, qint64(0));
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		const auto totals = runner.run(request, f.journals);
		QCOMPARE(totals.unchanged, 1);
		QCOMPARE(totals.succeeded, 0);
		QCOMPARE(totals.skipped, 0);
		QCOMPARE(totals.failed, 0);
		QVERIFY(!OpJournal::latestUndoable(f.journals));
		QVERIFY(OperationRecovery::pending(f.journals).isEmpty());
		QCOMPARE(get(f.src), f.bytes);
	}
}
void TestFileOperations::undo_already_restored_object_finishes_without_changes()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	QCOMPARE(runner.run(f.request(OpKind::Move), f.journals).succeeded, 1);
	const auto forward = OpJournal::scan(f.journals)[0];
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "copying") cancel = true;
	};
	QVERIFY(runner.run(undo, f.journals).cancelled);
	const auto pending = OpJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].request.kind, OpKind::Undo);
	// The user restores this exact object while Undo is stopped. A hard link
	// keeps both paths bound to the saved object without making either expendable.
	const QString copied = f.dest + "/clip.bin";
#ifdef Q_OS_WIN
	QVERIFY(::CreateHardLinkW(reinterpret_cast<LPCWSTR>(f.src.utf16()),
		reinterpret_cast<LPCWSTR>(copied.utf16()), nullptr));
#else
	QVERIFY(::link(QFile::encodeName(copied).constData(), QFile::encodeName(f.src).constData()) == 0);
#endif
	cancel = false;
	runner.hooks = {};
	OpRequest continuation;
	continuation.resumeJournalPath = pending[0].path;
	const auto totals = runner.run(continuation, f.journals);
	QCOMPARE(totals.unchanged, 1);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(totals.failed, 0);
	QVERIFY(OperationRecovery::pending(f.journals).isEmpty());
	QVERIFY(!OpJournal::latestUndoable(f.journals));
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(copied), f.bytes);
	const auto finished = OpJournal::readOne(pending[0].path);
	QVERIFY(finished);
	QCOMPARE(finished->entries[0].step, OpJournal::Step::NoEffect);
	QCOMPARE(finished->request.undoOf, forward.path);
}
void TestFileOperations::keep_both_retains_names_and_handles_late_race()
{
	Fixture f;
	put(f.dest + "/clip.bin", "original");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	bool raced = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "publishing" && !raced)
		{
			put(e.dst, "other writer");
			raced = true;
		}
	};
	const auto totals = runner.run(f.request(OpKind::Copy, "keepboth"), f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("original"));
	QCOMPARE(get(f.dest + "/clip (2).bin"), QByteArray("other writer"));
	QCOMPARE(get(f.dest + "/clip (3).bin"), f.bytes);
}
void TestFileOperations::skip_never_overwrites()
{
	Fixture f;
	put(f.dest + "/clip.bin", "original");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OpKind::Copy, "skip"), f.journals).skipped, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("original"));
}
void TestFileOperations::replace_is_rejected()
{
	Fixture f;
	put(f.dest + "/clip.bin", "original");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OpKind::Copy, "replace"), f.journals);
	QCOMPARE(t.succeeded, 0);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("original"));
}
void TestFileOperations::source_edit_is_retained()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	bool edited = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "before-readback")
		{
			QFile file(f.src);
			if (file.open(QIODevice::ReadWrite))
			{
				file.seek(3 * 1024 * 1024);
				edited = file.write("Z", 1) == 1;
				file.flush();
			}
		}
	};
	const auto t = runner.run(f.request(OpKind::Move), f.journals);
	if (edited)
	{
		QCOMPARE(t.succeeded, 0);
		QVERIFY(QFile::exists(f.src));
		QCOMPARE(get(f.src).at(3 * 1024 * 1024), 'Z');
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
	else
		QVERIFY(t.succeeded == 1 ||
				t.retained == 1); // enforced Windows sharing can reject the edit
}
void TestFileOperations::cancellation_preserves_replacement()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "before-readback")
		{
			put(e.dst, "other writer");
			cancel = true;
		}
	};
	auto t = runner.run(f.request(), f.journals);
	QVERIFY(t.cancelled);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("other writer"));
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::corrupt_readback_never_publishes()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	bool changed = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "before-readback")
		{
			QFile file(e.temp);
			if (file.open(QIODevice::ReadWrite))
			{
				file.seek(1);
				changed = file.write("Z", 1) == 1;
				file.flush();
			}
		}
	};
	auto t = runner.run(f.request(), f.journals);
	if (changed)
	{
		QCOMPARE(t.succeeded, 0);
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
	else
		QCOMPARE(t.succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::failed_cleanup_remains_journalled()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &point) { return point == "cleanup"; };
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "copy-chunk")
			cancel = true;
	};
	runner.run(f.request(), f.journals);
	const auto rec = OpJournal::scan(f.journals).first();
	QVERIFY(!rec.entries[0].artifacts.isEmpty());
	QVERIFY(QFile::exists(rec.entries[0].artifacts[0]));
	auto a = OperationRecovery::run(f.journals);
	auto b = OperationRecovery::run(f.journals);
	QVERIFY(a.opsFlagged > 0);
	QVERIFY(b.opsFlagged > 0);
	QVERIFY(QFile::exists(rec.path));
}
void TestFileOperations::journal_failure_stops_publication()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	bool failed = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "before-readback")
			failed = true;
	};
	runner.hooks.fail = [&](const QString &point) { return point == "journal" && failed; };
	const auto t = runner.run(f.request(), f.journals);
	QVERIFY(t.needsAttention > 0);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(QFile::exists(f.src));
	QCOMPARE(OpJournal::scan(f.journals).size(), 1);
}
void TestFileOperations::missing_journal_prevents_relocation()
{
	Fixture f;
	put(f.journals, "block");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OpKind::Move), f.journals);
	QCOMPARE(t.succeeded, 0);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::crash_before_verification_is_not_complete()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &)
	{
		if (stage == "before-readback")
			throw std::runtime_error("simulated termination");
	};
	runner.run(f.request(), f.journals);
	auto a = OperationRecovery::run(f.journals);
	QCOMPARE(a.resumable.size(), 1);
	QCOMPARE(a.resumable[0].finished, 0);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::crash_after_publication_is_reconciled()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &)
	{
		if (stage == "published")
			throw std::runtime_error("simulated termination");
	};
	runner.run(f.request(), f.journals);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	auto a = OperationRecovery::run(f.journals);
	QVERIFY(a.resumable.isEmpty());
	QCOMPARE(OpJournal::scan(f.journals)[0].entries[0].step, OpJournal::Step::Done);
}
void TestFileOperations::repeated_resume_continues_same_journal()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &)
	{
		if (stage == "before-readback")
			throw std::runtime_error("simulated termination");
	};
	runner.run(f.request(), f.journals);
	auto records = OpJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	OpRequest resume;
	resume.resumeJournalPath = records[0].path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(OpJournal::scan(f.journals).size(), 1);
	QCOMPARE(runner.run(resume, f.journals).succeeded, 0);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
}
void TestFileOperations::source_retention_is_explicit()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &s, const auto &) {
		if (s == "copies-complete") cancel = true;
	};
	auto t = runner.run(f.request(OpKind::Move), f.journals);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(sink.results.last().state, OpResult::State::SourceRetained);
	QVERIFY(!sink.results.last().sourceRemoved);
}
void TestFileOperations::same_volume_move_preserves_identity()
{
	Fixture f;
	const auto original = OpFile::inspect(f.src);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OpKind::Move), f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QVERIFY(original.sameObject(OpFile::inspect(f.dest + "/clip.bin")));
	QVERIFY(!QFile::exists(f.src));
	QVERIFY(sink.results.last().sourceRemoved);
}
void TestFileOperations::rename_group_conflict_skips_every_member()
{
	Fixture f;
	put(f.root + "/source/audio.bin", "audio");
	put(f.dest + "/clip.bin", "block");
	auto req = f.request(OpKind::Rename);
	req.items[0].renameDst = f.dest + "/clip.bin";
	req.items[0].groupKey = "clip";
	OpItem a;
	a.src = f.root + "/source/audio.bin";
	a.name = "audio.bin";
	a.bytes = 5;
	a.renameDst = f.dest + "/audio.bin";
	a.groupKey = "clip";
	req.items.append(a);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(req, f.journals).skipped, 2);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(QFile::exists(a.src));
}
void TestFileOperations::rename_failure_stops_group()
{
	Fixture f;
	auto req = f.request(OpKind::Rename);
	req.items[0].renameDst = f.dest + "/clip.bin";
	req.items[0].groupKey = "clip";
	OpItem a;
	a.src = f.root + "/source/audio.bin";
	a.name = "audio.bin";
	a.bytes = 5;
	a.renameDst = f.dest + "/audio.bin";
	a.groupKey = "clip";
	put(a.src, "audio");
	req.items.append(a);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &s) { return s == "relocate"; };
	QCOMPARE(runner.run(req, f.journals).failed, 1);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(QFile::exists(a.src));
}
void TestFileOperations::different_source_on_resume_is_refused()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &)
	{
		if (stage == "copying")
			throw std::runtime_error("stop");
	};
	runner.run(f.request(), f.journals);
	QFile::remove(f.src);
	put(f.src, QByteArray(f.bytes.size(), 'z'));
	auto a = OperationRecovery::run(f.journals);
	QVERIFY(a.opsFlagged > 0);
	QCOMPARE(get(f.src), QByteArray(f.bytes.size(), 'z'));
}
void TestFileOperations::corrupt_journal_is_preserved()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.run(f.request(), f.journals);
	const auto path = OpJournal::scan(f.journals)[0].path;
	QFile file(path);
	QVERIFY(file.open(QIODevice::Append));
	file.write("invalid complete record\n");
	file.close();
	auto a = OperationRecovery::run(f.journals);
	QVERIFY(a.opsFlagged > 0);
	QVERIFY(get(path).endsWith("invalid complete record\n"));
}
void TestFileOperations::trash_preserves_original_and_location()
{
	Fixture f;
	auto request = f.request(OpKind::Delete);
	request.diagnosticTrashRoot = f.root + "/_MediaMuster_Trash";
	const auto before = OpFile::inspect(f.src);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto e = OpJournal::scan(f.journals)[0].entries[0];
	QVERIFY(e.dst.startsWith(request.diagnosticTrashRoot + '/'));
	QVERIFY(before.sameObject(OpFile::inspect(e.dst)));
	QCOMPARE(get(e.dst), f.bytes);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(e.originalSource, f.src);
	QVERIFY(!e.originalVolume.rootPath.isEmpty());
	QVERIFY(!e.originalRelativePath.isEmpty());
}
void TestFileOperations::partial_group_failure_keeps_every_file_recorded()
{
	Fixture f;
	auto request = f.request(OpKind::Rename);
	request.items[0].renameDst = f.dest + "/clip.bin";
	request.items[0].groupKey = "relatives";
	auto other = request.items[0];
	other.src = f.root + "/source/audio.bin";
	other.name = "audio.bin";
	other.bytes = 5;
	other.renameDst = f.dest + "/audio.bin";
	put(other.src, "audio");
	request.items.append(other);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	int attempts = 0;
	runner.hooks.fail = [&](const QString &point)
	{ return point == "relocate" && ++attempts == 2; };
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(other.src), QByteArray("audio"));
	const auto record = OpJournal::scan(f.journals)[0];
	QCOMPARE(record.entries[0].step, OpJournal::Step::Done);
	QCOMPARE(record.entries[1].step, OpJournal::Step::Failed);
	QVERIFY(QFile::exists(record.path));
	OpRequest resume;
	resume.resumeJournalPath = record.path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(get(other.renameDst), QByteArray("audio"));
	QCOMPARE(OpJournal::scan(f.journals).size(), 1);
}
void TestFileOperations::same_volume_keep_both_handles_late_conflict()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	bool raced = false;
	runner.hooks.checkpoint = [&](const QString &point, const auto &e)
	{
		if (point == "relocating" && !raced)
		{
			put(e.dst, "other");
			raced = true;
		}
	};
	QCOMPARE(runner.run(f.request(OpKind::Move, "keepboth"), f.journals).succeeded, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("other"));
	QCOMPARE(get(f.dest + "/clip (2).bin"), f.bytes);
	QVERIFY(!QFile::exists(f.src));
}
void TestFileOperations::real_mxf_identity_is_checked()
{
	Fixture f;
	const QString sample = QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf");
	const auto header = MxfParser::parseHeader(sample);
	QVERIFY(header.hasMaterialPackage);
	QVERIFY(!header.fileMobId.isEmpty());
	OpRequest request;
	request.destRoot = f.dest;
	OpItem item;
	item.src = sample;
	item.name = "sample.mxf";
	item.bytes = QFileInfo(sample).size();
	item.mobId = header.fileMobId;
	item.masterMobId = header.umid;
	request.items.append(item);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	request.items[0].masterMobId =
		"060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a";
	request.items[0].name = "refused.mxf";
	QCOMPARE(runner.run(request, f.journals).failed, 1);
	QVERIFY(!QFile::exists(f.dest + "/refused.mxf"));
}
void TestFileOperations::mxf_parser_borrows_protected_handle()
{
	Fixture f;
	const QString sample = QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf");
	const auto expected = MxfParser::parseHeader(sample);
	const QString path = f.root + "/protected.mxf";
	QVERIFY(QFile::copy(sample, path));
	QString error;
	auto file = OpFile::open(path, false, error);
	QVERIFY2(file, qPrintable(error));
	const auto before = file->stamp();
	const int descriptor = file->io().handle();
	QVERIFY(file->io().seek(128));
	qint64 bytesRead = 0;
	const auto actual = MxfParser::parseHeader(file->io(), &bytesRead);
	QCOMPARE(actual.headerStatus, MediaMetadata::HeaderStatus::Complete);
	QCOMPARE(actual.fileMobId, expected.fileMobId);
	QCOMPARE(actual.umid, expected.umid);
	QVERIFY(bytesRead > 0);
	QVERIFY(file->io().isOpen());
	QCOMPARE(file->io().handle(), descriptor);
	QVERIFY(file->stillAt(path, before));
#ifdef Q_OS_WIN
	QVERIFY(file->protectedFromWriters());
	QFile writer(path);
	QVERIFY(!writer.open(QIODevice::ReadWrite));
#endif
	QFile closed;
	bytesRead = 123;
	QCOMPARE(MxfParser::parseHeader(closed, &bytesRead).headerStatus,
		MediaMetadata::HeaderStatus::IoError);
	QCOMPARE(bytesRead, 0);
}
void TestFileOperations::journal_volume_paths_survive_two_resolutions()
{
	Fixture f;
	const QString first = QFileInfo(f.src).absolutePath(), second = f.root + "/second",
				  third = f.root + "/third";
	auto request = f.request();
	request.destRoot = first + "/copy";
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(request, f.journals, error));
	}
	auto record = OpJournal::scan(f.journals)[0];
	auto original = VolumeIdentity::capture(first);
	original.rootPath = first;
	record.volumes = {original};
	QVERIFY(QDir().rename(first, second));
	auto mounted = original;
	mounted.rootPath = second;
	QVERIFY2(OpJournal::resolve(record, error, {mounted}), qPrintable(error));
	{
		OpJournal journal;
		QVERIFY(journal.resume(record, error));
	}
	auto read = OpJournal::readOne(record.path);
	QVERIFY(read);
	QCOMPARE(read->entries[0].item.src, second + "/clip.bin");
	QCOMPARE(read->request.destRoot, second + "/copy");
	QVERIFY(QDir().rename(second, third));
	mounted.rootPath = third;
	QVERIFY2(OpJournal::resolve(*read, error, {mounted}), qPrintable(error));
	{
		OpJournal journal;
		QVERIFY(journal.resume(*read, error));
	}
	const auto final = OpJournal::readOne(record.path);
	QVERIFY(final);
	QCOMPARE(final->entries[0].item.src, third + "/clip.bin");
	QCOMPARE(final->request.destRoot, third + "/copy");
	QCOMPARE(final->entries[0].originalSource, f.src);
}
void TestFileOperations::mismatched_volume_is_never_session_matched()
{
	Fixture f;
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(f.request(), f.journals, error));
	}
	auto record = OpJournal::scan(f.journals)[0];
	auto wrong = record.volumes[0];
	wrong.uuid = "another-volume";
	wrong.serial += 1;
	QVERIFY(!OpJournal::resolve(record, error, {wrong}));
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::second_runner_cannot_change_files()
{
	Fixture f;
	QString error;
	auto lock = OpJournal::acquire(f.journals, error);
	QVERIFY(lock);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OpKind::Move), f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::debug_harness_uses_disposable_files()
{
	Fixture f;
	OpDiagnostics::Options options;
	options.sourceArea = f.root;
	options.destinationArea = f.dest;
	options.reportArea = f.root + "/reports";
	options.samples = {QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf")};
	std::atomic<bool> cancel{false};
	const auto report = OpDiagnostics::run(options, cancel);
	QVERIFY2(QFile::exists(report.path), qPrintable(report.text));
	int bundledChecks = 0;
	for (const auto &value : report.json["checks"].toArray())
	{
		const auto check = value.toObject();
		if (check["name"].toString().startsWith("Bundled "))
		{
			++bundledChecks;
			QVERIFY2(check["status"].toString() == "passed",
					 qPrintable(check["name"].toString() + ": " + check["detail"].toString()));
		}
		if (check["name"].toString() == "Cross-filesystem source removal")
		{
			QCOMPARE(check["status"].toString(), QString("not tested"));
			continue;
		}
		QVERIFY2(check["status"].toString() != "failed" &&
					 check["status"].toString() != "unsupported",
				 qPrintable(report.text));
	}
	QCOMPARE(bundledChecks, 4);
	QCOMPARE(report.json["bundledSamples"].toArray().size(), 3);
#ifdef Q_OS_MAC
	struct statfs native{};
	QCOMPARE(::statfs(QFile::encodeName(f.root).constData(), &native), 0);
	const auto sourceVolume = report.json["source"].toObject();
	QCOMPARE(sourceVolume["device"].toString(), QFile::decodeName(native.f_mntfromname));
	QCOMPARE(sourceVolume["mount"].toString(), QFile::decodeName(native.f_mntonname));
	QCOMPARE(sourceVolume["readOnly"].toBool(), bool(native.f_flags & MNT_RDONLY));
#endif
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(QFile::exists(options.samples[0]));
}
void TestFileOperations::debug_setup_failure_saves_report()
{
	Fixture f;
	OpDiagnostics::Options options;
	options.sourceArea = f.root;
	options.destinationArea = f.src; // A file cannot hold the destination test folder.
	options.reportArea = f.root + "/reports";
	std::atomic<bool> cancel{false};
	const auto report = OpDiagnostics::run(options, cancel);
	QVERIFY2(QFile::exists(report.path), qPrintable(report.text));
	QCOMPARE(QJsonDocument::fromJson(get(report.path)).object(), report.json);
	const auto checks = report.json["checks"].toArray();
	QCOMPARE(checks.size(), 3); // Setup failure and the two untested/unsupported notices.
	QCOMPARE(checks[0].toObject()["name"].toString(), QString("Test setup or storage access"));
	QCOMPARE(checks[0].toObject()["status"].toString(), QString("failed"));
	QVERIFY(!report.json.contains("bundledSamples"));
	QVERIFY(QDir(report.json["sourceTestFolder"].toString()).isEmpty());
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::unsupported_directory_flush_preserves_originals()
{
	for (const auto kind : {OpKind::Copy, OpKind::Move})
	{
		Fixture f;
		put(f.dest + "/clip.bin", "existing media");
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		runner.hooks.directorySync = [](const QString &path, QString *error)
		{
			*error = "Directory flush unsupported: " + path;
			return NativeFile::SyncResult::OkDegraded;
		};
		int chunks = 0;
		bool raced = false;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &entry)
		{
			if (stage == "copy-chunk")
				++chunks;
			if (stage == "publishing" && !raced)
			{
				put(entry.dst, "late writer");
				raced = true;
			}
		};
		const auto totals = runner.run(f.request(kind, "keepboth"), f.journals);
		QCOMPARE(totals.succeeded, kind == OpKind::Copy ? 1 : 0);
		QCOMPARE(totals.retained, kind == OpKind::Move ? 1 : 0);
		QCOMPARE(totals.failed + totals.needsAttention, 0);
		QVERIFY(chunks > 0); // Native APIs choose their own chunk sizes.
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("existing media"));
		QCOMPARE(get(f.dest + "/clip (2).bin"), QByteArray("late writer"));
		QCOMPARE(get(f.dest + "/clip (3).bin"), f.bytes);
		QVERIFY(!sink.results.last().sourceRemoved);
		QVERIFY(sink.results.last().message.contains("does not support"));
		const auto entry = OpJournal::scan(f.journals)[0].entries[0];
		QVERIFY(!entry.hash.isEmpty());
		QVERIFY(!entry.copyDurable);
		QCOMPARE(entry.attempts, 1); // Late Keep Both conflicts reuse the completed copy.
		QVERIFY(!entry.source.sameObject(entry.landed));
	}
}
void TestFileOperations::directory_io_failure_stops_copy()
{
	for (const bool afterPublication : {false, true})
	{
		Fixture f;
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		bool publishing = false;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{ publishing = publishing || stage == "publishing"; };
		runner.hooks.directorySync = [&](const QString &path, QString *error)
		{
			if (!afterPublication || (publishing && path.contains("/.mediamuster-")))
			{
				*error = "Injected directory I/O error";
				return NativeFile::SyncResult::Failed;
			}
			if (publishing)
			{
				*error = "Directory flush unsupported";
				return NativeFile::SyncResult::OkDegraded;
			}
			return NativeFile::syncDirectory(path, error);
		};
		const auto totals = runner.run(f.request(), f.journals);
		QCOMPARE(totals.succeeded + totals.retained, 0);
		QCOMPARE(totals.failed + totals.needsAttention, 1);
		QCOMPARE(get(f.src), f.bytes);
		QVERIFY(sink.results.last().message.contains("Injected directory I/O error"));
		if (afterPublication)
		{
			QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
			const auto entry = OpJournal::scan(f.journals)[0].entries[0];
			QCOMPARE(entry.step, OpJournal::Step::NeedsAttention);
			QVERIFY(!entry.copyDurable);
		}
		else
			QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
}
void TestFileOperations::unsupported_relocation_keeps_media_and_databases()
{
	for (const auto kind : {OpKind::Delete, OpKind::Rename})
	{
		Fixture f;
		const auto database = f.root + "/source/msmMMOB.mdb";
		put(database, "original Avid database");
		auto request = f.request(kind);
		request.diagnosticTrashRoot = f.root + "/trash";
		request.items[0].renameDst = f.dest + "/clip.bin";
		request.items[0].groupKey = "relatives";
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		bool attemptedRelocation = false;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{ attemptedRelocation = attemptedRelocation || stage == "relocating"; };
		runner.hooks.directorySync = [](const QString &, QString *error)
		{
			*error = "Directory flush unsupported";
			return NativeFile::SyncResult::OkDegraded;
		};
		const auto totals = runner.run(request, f.journals);
		QCOMPARE(totals.succeeded + totals.retained, 0);
		QCOMPARE(totals.failed + totals.needsAttention, 1);
		QVERIFY(!attemptedRelocation);
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(database), QByteArray("original Avid database"));
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
		QCOMPARE(OpJournal::scan(f.journals)[0].entries.size(), 1);
	}
}
void TestFileOperations::directory_creation_propagates_durability()
{
	using Sync = NativeFile::SyncResult;
	for (const auto childResult : {Sync::Ok, Sync::Failed})
	{
		Fixture f;
		QString error;
		const auto status = OpFile::makeDirectory(f.root + "/parent/child", error,
			[&](const QString &path, QString *detail)
			{
				*detail = path == f.root ? "parent unsupported" : "child I/O error";
				return path == f.root ? Sync::OkDegraded : childResult;
			});
		QCOMPARE(status, childResult == Sync::Failed ? Sync::Failed : Sync::OkDegraded);
		QVERIFY(error.contains(childResult == Sync::Failed ? "child I/O error" : "parent unsupported"));
	}
}
void TestFileOperations::debug_harness_runs_copies_without_directory_flush()
{
	Fixture f;
	OpDiagnostics::Options options;
	options.sourceArea = f.root;
	options.destinationArea = f.dest;
	options.reportArea = f.root + "/reports";
	options.directorySync = [](const QString &path, QString *error)
	{
		*error = "Directory flush unsupported: " + path;
		return NativeFile::SyncResult::OkDegraded;
	};
	std::atomic<bool> cancel{false};
	const auto report = OpDiagnostics::run(options, cancel);
	QCOMPARE(report.json["schema"].toInt(), 3);
	QVERIFY(QFile::exists(report.path));
	QSet<QString> passed, unsupported;
	for (const auto &value : report.json["checks"].toArray())
	{
		const auto check = value.toObject();
		QVERIFY2(check["status"] != "failed", qPrintable(report.text));
		if (check["status"] == "passed")
			passed.insert(check["name"].toString());
		if (check["status"] == "unsupported")
			unsupported.insert(check["name"].toString());
	}
	for (const auto &name : {"Copy and readback", "Keep Both with a late conflict", "Skip",
							 "Cancel during copy", "Journal failure", "Move", "Bundled MXF copy and readback",
							 "Rebalance group conflict", "Bundled relatives group conflict"})
		QVERIFY2(passed.contains(name), name);
	for (const auto &name : {"Source directory persistence", "Destination directory persistence",
							 "MediaMuster Trash", "Rebalance relocation", "Bundled relatives Rebalance"})
		QVERIFY2(unsupported.contains(name), name);
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::bundled_samples_match_supplied_files()
{
	const QStringList expected{"5e2ed2597d8a95f15c8b3a9da7bfa97cc8142734fd1ac1707bff0ad7df0d26c4",
							   "76e4981ad8d844e10f12e3a91e44540228d80f7e04f51a10e584a90d13f242e5",
							   "efafabf1876cd7e7481017f29325e66314431ea7faeab91b41ffd8ec738ccc2e"};
	const auto samples = OpDiagnostics::bundledSamples();
	QCOMPARE(samples.size(), expected.size());
	QSet<QString> masters, fileMobs;
	for (int i = 0; i < samples.size(); ++i)
	{
		QVERIFY2(OpFile::safePath(samples[i]), qPrintable(samples[i]));
		QCOMPARE(OpFile::inspect(samples[i]).size, QFileInfo(samples[i]).size());
		QFile file(samples[i]);
		QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(samples[i]));
		QCryptographicHash hash(QCryptographicHash::Sha256);
		QVERIFY(hash.addData(&file));
		QCOMPARE(QString::fromLatin1(hash.result().toHex()), expected[i]);
		const auto header = MxfParser::parseHeader(samples[i]);
		QCOMPARE(header.headerStatus, MediaMetadata::HeaderStatus::Complete);
		QVERIFY(header.hasMaterialPackage);
		masters.insert(header.umid);
		fileMobs.insert(header.fileMobId);
	}
	QCOMPARE(masters.size(), 1);
	QCOMPARE(fileMobs.size(), 3);
}
void TestFileOperations::selected_duplicates_keep_both()
{
	Fixture f;
	auto request = f.request();
	auto second = request.items[0];
	second.src = f.root + "/second/clip.bin";
	put(second.src, "other");
	second.bytes = 5;
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 2);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.dest + "/clip (2).bin"), QByteArray("other"));
}
void TestFileOperations::regenerated_database_is_retired_on_resume()
{
	Fixture f;
	auto request = f.request(OpKind::Rename);
	request.items[0].renameDst = f.dest + "/clip.bin";
	request.diagnosticTrashRoot = f.root + "/_MediaMuster_Trash";
	const auto database = f.root + "/source/msmMMOB.mdb";
	put(database, "original database");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &e)
	{
		if (stage == "relocating" && !e.item.maintenance)
			throw std::runtime_error("interrupt before media move");
	};
	runner.run(request, f.journals);
	QVERIFY(!QFile::exists(database));
	QVERIFY(QFile::exists(f.src));
	const auto record = OpJournal::scan(f.journals)[0];
	put(database, "regenerated database");
	OpRequest resume;
	resume.resumeJournalPath = record.path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	const auto final = OpJournal::scan(f.journals)[0];
	QCOMPARE(final.entries.size(), 3);
	QCOMPARE(get(final.entries[1].dst), QByteArray("original database"));
	QCOMPARE(get(final.entries[2].dst), QByteArray("regenerated database"));
	QVERIFY(!QFile::exists(database));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
}
void TestFileOperations::torn_tail_retains_verified_prefix()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &)
	{
		if (stage == "published")
			throw std::runtime_error("interrupted");
	};
	runner.run(f.request(), f.journals);
	const auto record = OpJournal::scan(f.journals)[0];
	const auto prefix = get(record.path);
	{
		QFile file(record.path);
		QVERIFY(file.open(QIODevice::Append));
		file.write("{unfinished");
	}
	OperationRecovery::run(f.journals);
	QVERIFY(get(record.path).startsWith(prefix));
	QVERIFY(!OpJournal::scan(f.journals)[0].torn);
	QCOMPARE(OpJournal::scan(f.journals)[0].entries[0].step, OpJournal::Step::Done);
}
void TestFileOperations::abrupt_process_exit_is_recoverable()
{
	for (const auto &checkpoint : {QString("before-readback"), QString("published")})
	{
		Fixture f;
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(),
					{"--crash-copy", checkpoint, f.src, f.dest, f.journals});
		QVERIFY(child.waitForFinished(30000));
		QCOMPARE(child.exitCode(), 81);
		const auto records = OpJournal::scan(f.journals);
		QCOMPARE(records.size(), 1);
		QCOMPARE(get(f.src), f.bytes);
		if (checkpoint == "before-readback")
		{
			QCOMPARE(QFileInfo(records[0].entries[0].temp).size(), qint64(f.bytes.size()));
			QCOMPARE(OperationRecovery::run(f.journals).resumable.size(), 1);
			QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
		}
		else
		{
			QVERIFY(OperationRecovery::run(f.journals).resumable.isEmpty());
			QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		}
	}
}
void TestFileOperations::rebalance_late_conflict_stops_remaining_members()
{
	Fixture f;
	auto request = f.request(OpKind::Rename);
	request.items.clear();
	for (int n = 0; n < 3; ++n)
	{
		OpItem item;
		item.name = QString::number(n) + ".bin";
		item.src = f.root + "/source/" + item.name;
		item.renameDst = f.dest + '/' + item.name;
		item.bytes = 5;
		item.groupKey = "same clip";
		put(item.src, "media");
		request.items.append(item);
	}
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "relocating" && e.item.name == "1.bin")
			put(e.dst, "other writer");
	};
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(get(f.dest + "/0.bin"), QByteArray("media"));
	QCOMPARE(get(f.dest + "/1.bin"), QByteArray("other writer"));
	QVERIFY(QFile::exists(request.items[1].src));
	QVERIFY(QFile::exists(request.items[2].src));
	QVERIFY(!QFile::exists(f.dest + "/2.bin"));
}
#ifdef Q_OS_WIN
void TestFileOperations::encrypted_windows_copy_is_refused()
{
	Fixture f;
	const auto native = QDir::toNativeSeparators(f.src);
	if (!::EncryptFileW(reinterpret_cast<const wchar_t *>(native.utf16())))
		QSKIP("EFS is unavailable on this test configuration.");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	QCOMPARE(runner.run(f.request(OpKind::Move), f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(sink.results.last().message.contains("Encrypted"));
}
#endif
void TestFileOperations::verification_off_avoids_readback()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	bool readback = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		readback = readback || stage == "before-readback" || stage == "readback-chunk";
	};
	auto request = f.request();
	request.verifyCopies = false;
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	QVERIFY(!readback);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	const auto record = OpJournal::scan(f.journals).first();
	QVERIFY(record.entries.first().hash.isEmpty());
	QVERIFY(!record.request.verifyCopies);
}
void TestFileOperations::move_copies_every_file_before_removal()
{
	Fixture f;
	auto request = f.request(OpKind::Move);
	request.verifyCopies = false;
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	int published = 0;
	bool intact = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "published") {
			++published;
			intact = intact && get(f.src) == f.bytes && get(second.src) == f.bytes;
		}
		if (stage == "before-source-retirement") intact = intact && published == 2;
	};
	const auto t = runner.run(request, f.journals);
	QVERIFY2(t.succeeded == 2, qPrintable(sink.messages.join('\n')));
	QVERIFY(intact);
	QVERIFY(!QFile::exists(f.src));
	QVERIFY(!QFile::exists(second.src));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	QCOMPARE(sink.results.size(), 2);
	QVERIFY(OpJournal::scan(f.journals)[0].copiesComplete);
}
void TestFileOperations::failed_copy_continues_and_blocks_all_original_removal()
{
	Fixture f;
	auto request = f.request(OpKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	put(f.dest + "/clip.bin", "unexpected conflict");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	const auto t = runner.run(request, f.journals);
	QCOMPARE(t.failed, 1);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(second.src), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	QVERIFY(!OpJournal::scan(f.journals)[0].copiesComplete);
}
void TestFileOperations::explicit_skip_survives_disappearing_conflict()
{
	Fixture f;
	auto request = f.request(OpKind::Move, "skip");
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	second.policy.clear();
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	const auto t = runner.run(request, f.journals);
	QCOMPARE(t.skipped, 1);
	QCOMPARE(t.succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(second.src));
}
void TestFileOperations::removal_crash_boundaries_resume()
{
	for (const auto &point : {"copies-complete", "before-source-retirement", "source-retired", "source-unlinked"})
	{
		Fixture f;
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		runner.hooks.forceCopy = true;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
			if (stage == point) throw std::runtime_error("crash boundary");
		};
		auto request = f.request(OpKind::Move);
		request.verifyCopies = false;
		QVERIFY(runner.run(request, f.journals).needsAttention > 0);
		auto saved = OpJournal::scan(f.journals).first();
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		OpRequest resume;
		resume.resumeJournalPath = saved.path;
		runner.hooks = {};
		const auto t = runner.run(resume, f.journals);
		QVERIFY2(t.needsAttention == 0 && t.failed == 0,
			qPrintable(QString(point) + ": " + sink.messages.join('\n')));
		QVERIFY(!QFile::exists(f.src));
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		QVERIFY(OpJournal::readOne(saved.path)->entries[0].complete());
	}
}
void TestFileOperations::undo_copy_is_gated_and_claims_forward()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(), f.journals).succeeded, 1);
	const auto forward = OpJournal::scan(f.journals).first();
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoJournalPath = forward.path;
	QVERIFY(runner.run(undo, f.journals).needsAttention > 0);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	undo.undoEnabled = true;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!OpJournal::readOne(forward.path)->undoPath.isEmpty());
	QVERIFY(!OpJournal::latestUndoable(f.journals));
	OpRequest resume;
	resume.resumeJournalPath = forward.path;
	QVERIFY(runner.run(resume, f.journals).needsAttention > 0);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::undo_partial_move_restores_only_completed_changes()
{
	Fixture f;
	auto request = f.request(OpKind::Move);
	request.verifyCopies = false;
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e) {
		if (stage == "source-removed" && e.id == 0) cancel = true;
	};
	const auto first = runner.run(request, f.journals);
	QVERIFY(first.cancelled);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(second.src), f.bytes);
	const auto forward = OpJournal::scan(f.journals).first();
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	cancel = false;
	runner.hooks = {};
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 2, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(second.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(f.dest + "/audio.bin"));
}
void TestFileOperations::undo_interrupted_copy_and_changed_result()
{
	Fixture f;
	auto request = f.request();
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e) {
		if (stage == "done" && e.id == 0) cancel = true;
	};
	QVERIFY(runner.run(request, f.journals).cancelled);
	const auto forward = OpJournal::scan(f.journals).first();
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	cancel = false;
	runner.hooks = {};
	QCOMPARE(runner.run(undo, f.journals).succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(second.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(f.dest + "/audio.bin"));

	Fixture changed;
	QCOMPARE(runner.run(changed.request(), changed.journals).succeeded, 1);
	undo.undoJournalPath = OpJournal::scan(changed.journals).first().path;
	put(changed.dest + "/clip.bin", "a changed file");
	QVERIFY(runner.run(undo, changed.journals).needsAttention > 0);
	QCOMPARE(get(changed.dest + "/clip.bin"), QByteArray("a changed file"));
}
void TestFileOperations::undo_move_resume_keeps_saved_policy()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	auto request = f.request(OpKind::Move);
	request.verifyCopies = false;
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = OpJournal::scan(f.journals).first().path;
	runner.hooks = {};
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "published") throw std::runtime_error("interrupt Undo");
	};
	QVERIFY(runner.run(undo, f.journals).needsAttention > 0);
	const auto pending = OpJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].request.kind, OpKind::Undo);
	QVERIFY(!pending[0].request.verifyCopies);
	OpRequest resume;
	resume.resumeJournalPath = pending[0].path;
	resume.verifyCopies = true;
	bool readback = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "before-readback") readback = true;
	};
	const auto t = runner.run(resume, f.journals);
	QVERIFY2(t.needsAttention == 0 && t.failed == 0, qPrintable(sink.messages.join('\n')));
	QVERIFY(!readback);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::local_trash_and_undo_roundtrip()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	struct RestoreDisposable
	{
		Fixture &fixture;
		std::atomic<bool> &cancel;
		~RestoreDisposable()
		{
			if (QFile::exists(fixture.src)) return;
			cancel.store(false);
			for (const auto &record : OpJournal::scan(fixture.journals))
				for (const auto &entry : record.entries)
					if (!entry.trashReceipt.isEmpty() && entry.landed.valid())
					{
						const auto restored = OpTrash::restore(entry.trashReceipt, fixture.src,
							entry.landed, cancel, entry.dst);
						if (QFile::exists(fixture.src)) return;
						qWarning().noquote() << "Disposable Trash restoration:" << restored.error;
					}
			fixture.dir.setAutoRemove(false);
			qWarning().noquote() << "Disposable Trash recovery record retained:" << fixture.journals;
		}
	} restoreDisposable{f, cancel};
	auto details = [&]
	{
		QStringList messages = sink.messages;
		for (const auto &result : sink.results)
			messages.append(result.message + " Source: " + result.source + " Destination: " + result.destination);
		return messages.join('\n');
	};
	OpRunner runner(sink, cancel);
	auto request = f.request(OpKind::Delete);
	request.diagnosticTrashRoot.clear();
	const auto first = runner.run(request, f.journals);
	QVERIFY2(first.succeeded == 1, qPrintable(details()));
	QVERIFY(!QFile::exists(f.src));
	const auto forward = OpJournal::scan(f.journals).first();
	if (forward.entries[0].trashProvider == "system")
		QVERIFY(!forward.entries[0].trashReceipt.isEmpty());
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(details()));
	QCOMPARE(get(f.src), f.bytes);
}

void TestFileOperations::retry_is_bounded_and_journalled()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	int failures = 0;
	runner.hooks.nativeCopyError = [&](const auto &) { return failures++ == 0 ? transientCopyError : 0; };
	QCOMPARE(runner.run(f.request(), f.journals).succeeded, 1);
	QCOMPARE(OpJournal::scan(f.journals)[0].entries[0].attempts, 2);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);

	Fixture failed;
	runner.hooks.nativeCopyError = [](const auto &) { return transientCopyError; };
	QCOMPARE(runner.run(failed.request(), failed.journals).failed, 1);
	QCOMPARE(OpJournal::scan(failed.journals)[0].entries[0].attempts, 3);
	QCOMPARE(get(failed.src), failed.bytes);
	QVERIFY(!QFile::exists(failed.dest + "/clip.bin"));
}
void TestFileOperations::native_copy_error_classification_data()
{
	QTest::addColumn<int>("nativeError");
	QTest::addColumn<bool>("retryable");
	QTest::newRow("success") << 0 << false;
#ifdef Q_OS_WIN
	QTest::newRow("busy") << int(ERROR_BUSY) << true;
	QTest::newRow("network-busy") << int(ERROR_NETWORK_BUSY) << true;
	QTest::newRow("not-ready") << int(ERROR_NOT_READY) << true;
	QTest::newRow("timeout") << int(ERROR_SEM_TIMEOUT) << true;
	QTest::newRow("retry") << int(ERROR_RETRY) << true;
	QTest::newRow("sharing") << int(ERROR_SHARING_VIOLATION) << true;
	QTest::newRow("locked") << int(ERROR_LOCK_VIOLATION) << true;
	QTest::newRow("permission") << int(ERROR_ACCESS_DENIED) << false;
	QTest::newRow("full") << int(ERROR_DISK_FULL) << false;
	QTest::newRow("io-error") << int(ERROR_CRC) << false;
	QTest::newRow("invalid") << int(ERROR_INVALID_PARAMETER) << false;
	QTest::newRow("cancelled") << int(ERROR_OPERATION_ABORTED) << false;
#else
	QTest::newRow("busy") << EBUSY << true;
	QTest::newRow("again") << EAGAIN << true;
	QTest::newRow("interrupted") << EINTR << true;
	QTest::newRow("timeout") << ETIMEDOUT << true;
	QTest::newRow("permission") << EACCES << false;
	QTest::newRow("full") << ENOSPC << false;
	QTest::newRow("io-error") << EIO << false;
	QTest::newRow("invalid") << EINVAL << false;
	QTest::newRow("cancelled") << ECANCELED << false;
#endif
}
void TestFileOperations::native_copy_error_classification()
{
	QFETCH(int, nativeError);
	QFETCH(bool, retryable);
	QCOMPARE(OpCopier::isRetryableNativeError(nativeError), retryable);
}
void TestFileOperations::native_copy_retry_policy_data()
{
	QTest::addColumn<int>("nativeError");
	QTest::addColumn<int>("failureCount");
	QTest::addColumn<int>("expectedAttempts");
	QTest::addColumn<bool>("copiedAll");
	QTest::newRow("transient-recovers") << transientCopyError << 1 << 2 << true;
	QTest::newRow("permanent-continues") << permanentCopyError << 1 << 1 << false;
	QTest::newRow("exhausted-continues") << transientCopyError << 10 << 3 << false;
}
void TestFileOperations::native_copy_retry_policy()
{
	QFETCH(int, nativeError);
	QFETCH(int, failureCount);
	QFETCH(int, expectedAttempts);
	QFETCH(bool, copiedAll);
	Fixture f;
	auto request = f.request(OpKind::Move);
	request.verifyCopies = false;
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	int firstCalls = 0, secondCalls = 0;
	QStringList stagingPaths;
	runner.hooks.nativeCopyError = [&](const OpJournal::Entry &entry) {
		if (entry.id != 0)
		{
			++secondCalls;
			return 0;
		}
		stagingPaths.append(entry.temp);
		return ++firstCalls <= failureCount ? nativeError : 0;
	};
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(firstCalls, expectedAttempts);
	QCOMPARE(secondCalls, 1);
	QCOMPARE(totals.failed, copiedAll ? 0 : 1);
	QCOMPARE(totals.succeeded, copiedAll ? 2 : 0);
	QCOMPARE(totals.retained, copiedAll ? 0 : 1);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	const auto records = OpJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const auto &record = records[0];
	QCOMPARE(record.entries[0].attempts, expectedAttempts);
	QCOMPARE(record.entries[1].attempts, 1);
	QCOMPARE(record.copiesComplete, copiedAll);
	QCOMPARE(sink.results.size(), 2);
	QSet<QString> uniqueStaging(stagingPaths.cbegin(), stagingPaths.cend());
	QCOMPARE(uniqueStaging.size(), expectedAttempts);
	for (const auto &path : stagingPaths)
		if (OpFile::occupied(path))
			QVERIFY(record.entries[0].artifacts.contains(path));
	if (copiedAll)
	{
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		QVERIFY(!QFile::exists(f.src));
		QVERIFY(!QFile::exists(second.src));
	}
	else
	{
		QCOMPARE(record.entries[0].step, OpJournal::Step::Failed);
		QVERIFY(!record.entries[0].error.isEmpty());
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(second.src), f.bytes);
	}
}
void TestFileOperations::native_copy_retry_cancellation_data()
{
	QTest::addColumn<bool>("duringCopy");
	QTest::newRow("native-copy-cancelled") << true;
	QTest::newRow("before-retry") << false;
}
void TestFileOperations::native_copy_retry_cancellation()
{
	QFETCH(bool, duringCopy);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	int calls = 0;
	runner.hooks.nativeCopyError = [&](const auto &) {
		++calls;
		if (duringCopy) cancel = true;
		return transientCopyError;
	};
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "before-copy-retry") cancel = true;
	};
	const auto totals = runner.run(f.request(), f.journals);
	QVERIFY(totals.cancelled);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.succeeded, 0);
	QCOMPARE(calls, 1);
	const auto records = OpJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records[0].entries[0].attempts, 1);
	QCOMPARE(records[0].entries[0].step, OpJournal::Step::Cancelled);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::native_copy_retry_stops_on_journal_failure()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	int calls = 0;
	bool failJournal = false;
	runner.hooks.nativeCopyError = [&](const auto &) {
		++calls;
		failJournal = true;
		return transientCopyError;
	};
	runner.hooks.fail = [&](const QString &stage) { return stage == "journal" && failJournal; };
	const auto totals = runner.run(f.request(), f.journals);
	QVERIFY(totals.needsAttention > 0);
	QCOMPARE(totals.succeeded, 0);
	QCOMPARE(calls, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::publication_failure_does_not_recopy()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	int publications = 0;
	runner.hooks.fail = [&](const QString &stage) { return stage == "publish" && ++publications == 1; };
	const auto totals = runner.run(f.request(), f.journals);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(publications, 1);
	QCOMPARE(OpJournal::scan(f.journals)[0].entries[0].attempts, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::undo_rebalance_retires_regenerated_indexes()
{
	Fixture f;
	put(f.root + "/source/msmMMOB.mdb", "original database");
	auto request = f.request(OpKind::Rename);
	request.items[0].renameDst = f.dest + "/clip.bin";
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto forward = OpJournal::scan(f.journals)[0];
	put(f.root + "/source/msmMMOB.mdb", "regenerated source");
	put(f.dest + "/msmFMID.pmr", "regenerated destination");
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(f.root + "/source/msmMMOB.mdb"));
	QVERIFY(!QFile::exists(f.dest + "/msmFMID.pmr"));
	const auto inverse = OpJournal::readOne(OpJournal::readOne(forward.path)->undoPath);
	QVERIFY(inverse);
	int retired = 0;
	for (const auto &entry : inverse->entries)
		if (entry.item.maintenance) { ++retired; QVERIFY(entry.complete()); }
	QCOMPARE(retired, 2);
}
void TestFileOperations::undo_original_in_retirement()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "source-retired") cancel = true;
	};
	QVERIFY(runner.run(f.request(OpKind::Move), f.journals).cancelled);
	const auto forward = OpJournal::scan(f.journals)[0];
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(forward.entries[0].retirement), f.bytes);
	QVERIFY(OpJournal::latestUndoable(f.journals));
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	cancel = false;
	runner.hooks = {};
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.needsAttention == 0 && t.failed == 0, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(forward.entries[0].retirement));
}

void TestFileOperations::network_delete_always_uses_mediamuster_trash()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceNetworkTrash = true;
	auto request = f.request(OpKind::Delete);
	request.diagnosticTrashRoot.clear();
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto saved = OpJournal::scan(f.journals)[0];
	QCOMPARE(saved.entries[0].trashProvider, QString("mediamuster"));
	QVERIFY(saved.entries[0].trashReceipt.isEmpty());
	QVERIFY(saved.entries[0].dst.contains("/_MediaMuster_Trash/"));
	QCOMPARE(get(saved.entries[0].dst), f.bytes);
	QVERIFY(!QFile::exists(f.src));
}

void TestFileOperations::resume_flush_failure_stays_unfinished()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &) {
		if (stage == "published") throw std::runtime_error("interrupted copy");
	};
	QVERIFY(runner.run(f.request(), f.journals).needsAttention > 0);
	const auto saved = OpJournal::scan(f.journals)[0];
	OpRequest resume;
	resume.resumeJournalPath = saved.path;
	runner.hooks = {};
	runner.hooks.directorySync = [](const QString &, QString *error) {
		*error = "Injected directory I/O failure";
		return NativeFile::SyncResult::Failed;
	};
	QVERIFY(runner.run(resume, f.journals).needsAttention > 0);
	QVERIFY(!OpJournal::readOne(saved.path)->entries[0].complete());
	QCOMPARE(get(f.src), f.bytes);
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).needsAttention, 0);
	QVERIFY(OpJournal::readOne(saved.path)->entries[0].complete());
}
void TestFileOperations::undo_late_collision_remains_resumable()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OpKind::Move), f.journals).succeeded, 1);
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = OpJournal::scan(f.journals)[0].path;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e) {
		if (stage == "relocating") put(e.dst, "late conflict");
	};
	const auto failed = runner.run(undo, f.journals);
	QVERIFY(failed.failed + failed.needsAttention > 0);
	QCOMPARE(get(f.src), QByteArray("late conflict"));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	const auto pending = OpJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QVERIFY(QFile::remove(f.src));
	OpRequest resume;
	resume.resumeJournalPath = pending[0].path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::resume_continues_past_failed_source()
{
	Fixture f;
	auto request = f.request(OpKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	put(f.dest + "/clip.bin", "unexpected conflict");
	Sink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &) {
		if (stage == "failed") cancel = true;
	};
	QVERIFY(runner.run(request, f.journals).cancelled);
	const auto saved = OpJournal::scan(f.journals)[0];
	QVERIFY(QFile::remove(f.src)); // A failed source disappears while the app is stopped.
	OpRequest resume;
	resume.resumeJournalPath = saved.path;
	cancel = false;
	runner.hooks = {};
	const auto t = runner.run(resume, f.journals);
	QCOMPARE(t.failed, 1);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(second.src), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	QVERIFY(!OpJournal::readOne(saved.path)->copiesComplete);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	if (args.size() == 6 && args[1] == "--crash-copy")
	{
		Sink sink;
		std::atomic<bool> cancel{false};
		OpRunner runner(sink, cancel);
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{
			if (stage == args[2])
				std::_Exit(81);
		};
		OpRequest request;
		request.verifyCopies = true;
		request.destRoot = args[4];
		OpItem item;
		item.src = args[3];
		item.name = "clip.bin";
		item.bytes = QFileInfo(item.src).size();
		request.items.append(item);
		runner.run(request, args[5]);
		return 82;
	}
	TestFileOperations test;
	return QTest::qExec(&test, argc, argv);
}
#include "tst_fileoperations.moc"
