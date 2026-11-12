#include "test_bento.h"
#include "scanner/bella_scanner.h"
#include "media/media_file_record.h"
#include "operations/operation_runner.h"
#include "operations/operation_manager.h"
#include "operations/operation_request.h"
#include "operations/rebalancer.h"
#include "operations/rebalance_planner.h"
#include "media/mob_id_text.h"
#include "operations/operation_plan.h"
#include "operations/operation_recovery.h"
#include "operations/operation_trash.h"
#include "scanner/mxf_parser.h"
#include "scanner/omf_parser.h"
#include "media/omf_uid.h"
#include "ui/scan_result_p.h"
#include <QTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <memory>
#include <utility>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QEvent>
#include <QProcess>
#include <QCryptographicHash>
#include <cstdlib>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <stdexcept>
#include <limits>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

// MARK: - Test helpers

namespace
{
	OperationHeaderIdentity readHeaderIdentity(const QString &path, bool omf = false)
	{
		const std::atomic_bool stop{false};
		OperationHeaderIdentity identity;
		const auto capture = [&](const auto &report)
		{
			if (report.fileInformation.fileMobId)
				identity.fileMobId = OmfUid::toIdText(report.fileInformation.fileMobId->bytes());
			if (report.associatedClipInformation.masterMobId)
				identity.masterMobIds.append(OmfUid::toIdText(report.associatedClipInformation.masterMobId->bytes()));
		};
		if (omf)
			capture(Bella::parseOmf(path, stop, false));
		else
			capture(Bella::parseMxf(path, stop, false));
		return identity;
	}

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
	QByteArray omfForOperationIdentity(const QByteArray &fileId)
	{
		using TestMediaEngineBento::number;
		TestMediaEngineBento::TypedBento writer;
		writer.head();
		writer.referenceArray(1, "OMFI:ObjectSpine", {100, 300});
		writer.add(100, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
		writer.add(100, "OMFI:MOBJ:MobID", "omfi:UID", fileId);
		writer.add(100, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(200));
		writer.add(200, "OMFI:ObjID", "omfi:ObjectTag", "CDCI", true);
		writer.add(200, "OMFI:MDFL:SampleRate", "omfi:ExactEditRate", number<qint32>(25) + number<qint32>(1));
		writer.add(200, "OMFI:MDFL:Length", "omfi:Long", number<qint32>(1), true);
		writer.add(300, "OMFI:ObjID", "omfi:ObjectTag", "TIFF", true);
		writer.add(300, "OMFI:TIFF:MobID", "omfi:UID", fileId);
		return writer.build();
	}
	QByteArray mxfForOperationIdentity(const QByteArray &fileId)
	{
		using TestMediaEngineBento::number;
		const auto hex = &QByteArray::fromHex;
		const auto packet = [&](const QByteArray &key, const QByteArray &value)
		{
			// Each packet in this small header uses a one-byte BER length.
			Q_ASSERT(value.size() < 128);
			return key + QByteArray(1, char(value.size())) + value;
		};
		const auto field = [&](quint16 tag, const QByteArray &value)
		{ return number(tag, true) + number(quint16(value.size()), true) + value; };
		const auto localSet = [&](const char *key, const QByteArray &instance, const QByteArray &fields)
		{ return packet(hex(key), field(0x8101, instance) + fields); };
		const auto reference = [&](const QByteArray &instance)
		{ return hex("0000000100000010") + instance; };

		const QByteArray storage(16, 'c'), package(16, 's'), essence(16, 'e');
		// Primer mappings for InstanceUID, ContentStorage, Packages,
		// EssenceContainerData, LinkedPackageUID and PackageUID respectively.
		const auto primer = hex("0000000600000012"
								"8101060e2b34010101010101150200000000"
								"8102060e2b34010101020601010402010000"
								"8103060e2b34010101020601010405010000"
								"8104060e2b34010101020601010405020000"
								"8105060e2b34010101020601010601000000"
								"8106060e2b34010101010101151000000000");
		const auto metadata = packet(hex("060e2b34020501010d01020101050100"), primer) +
							  localSet("060e2b34025301010d01010101012f00", QByteArray(16, 'p'), field(0x8102, storage)) +
							  localSet("060e2b34025301010d01010101011800", storage,
									   field(0x8103, reference(package)) + field(0x8104, reference(essence))) +
							  localSet("060e2b34025301010d01010101013700", package, field(0x8106, fileId)) +
							  localSet("060e2b34025301010d01010101012300", essence, field(0x8105, fileId));
		const auto partition = hex("0001000300000001") + QByteArray(24, '\0') +
							   number(quint64(metadata.size()), true) + QByteArray(24, '\0') +
							   hex("060e2b34040101020d010201100300000000000000000010");
		return packet(hex("060e2b34020501010d01020101020400"), partition) + metadata;
	}
	Bella::ScanEntry operationEntryFor(const QString &path)
	{
		const QFileInfo file(path);
		Bella::ScanEntry entry;
		entry.path = path;
		entry.sizeBytes = file.size();
		entry.modified = file.lastModified();
		entry.volumeIdentifier = VolumeIdentity::capture(path).identifier();
		return entry;
	}
	enum class BellaOperationCase
	{
		HeaderMatch,
		HeaderMismatch,
		DatabaseMatch,
		DatabaseMismatch,
		PhysicalOnly,
		Changed,
		PresenceUnconfirmed
	};
	QStringList privateDirectories(const QString &root)
	{
		QStringList out;
		QDirIterator it(root, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
						QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const auto path = it.next();
			if (QFileInfo(path).fileName().startsWith(".mediamuster-"))
				out.append(path);
		}
		return out;
	}
	auto interruptAt(const QString &checkpoint)
	{
		return [checkpoint](const QString &stage, const auto &)
		{
			if (stage == checkpoint)
				throw std::runtime_error("simulated termination at " + checkpoint.toStdString());
		};
	}
	struct Sink : OperationSink
	{
		QVector<OperationResult> results;
		QStringList messages;
		QVector<QVector<OperationTrashFallbackItem>> trashFallbackPrompts;
		std::function<bool(const QVector<OperationTrashFallbackItem> &)> trashFallbackAnswer;
		bool confirmTrashFallback(const QVector<OperationTrashFallbackItem> &items) override
		{
			trashFallbackPrompts.append(items);
			return trashFallbackAnswer && trashFallbackAnswer(items);
		}
		void progress(const QString &, int, int, double) override {}
		void log(QtMsgType, const QString &s) override
		{
			messages.append(s);
		}
		void trashUsed(const QString &, int) override {}
		void result(const OperationResult &r) override
		{
			results.append(r);
		}
	};
	QStringList copyMoveSummaries(const Sink &sink)
	{
		return sink.messages.filter(QRegularExpression(QStringLiteral("^(Copy|Move) (complete|cancelled|incomplete):")));
	}
	struct Fixture
	{
		QTemporaryDir dir;
		QString root = OperationJournal::canonicalPath(dir.path());
		QString src = root + "/source/clip.bin", dest = root + "/destination",
				journals = root + "/journals";
		QByteArray bytes = QByteArray(8 * 1024 * 1024, 'x');
		Fixture()
		{
			put(src, bytes);
			QDir().mkpath(dest);
		}
		OperationRequest request(OperationKind kind = OperationKind::Copy, QString policy = {}) const
		{
			OperationRequest r;
			r.kind = kind;
			r.diagnosticTrashRoot = root + "/_MediaMuster_Trash";
			r.destRoot = dest;
			OperationItem i;
			i.src = src;
			i.name = "clip.bin";
			i.bytes = bytes.size();
			i.policy = policy;
			r.items.append(i);
			return r;
		}
	};
	// Scope the asynchronous facades' default journal location to disposable storage.
	// Facades are destroyed before this guard, so workers finish before it is restored.
	class ScopedJournalDirectory
	{
	public:
		explicit ScopedJournalDirectory(const QString &path)
			: m_hadValue(qEnvironmentVariableIsSet("MEDIAMUSTER_JOURNAL_DIR")),
			  m_previousValue(qgetenv("MEDIAMUSTER_JOURNAL_DIR"))
		{
			if (!qputenv("MEDIAMUSTER_JOURNAL_DIR", path.toUtf8()))
				qFatal("Cannot isolate the operation test journal directory");
		}
		~ScopedJournalDirectory()
		{
			if (m_hadValue)
				qputenv("MEDIAMUSTER_JOURNAL_DIR", m_previousValue);
			else
				qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
		}

	private:
		bool m_hadValue;
		QByteArray m_previousValue;
	};
#ifdef Q_OS_WIN
	constexpr int transientCopyError = ERROR_NETWORK_BUSY;
	constexpr int permanentCopyError = ERROR_ACCESS_DENIED;
#else
	constexpr int transientCopyError = EBUSY;
	constexpr int permanentCopyError = EACCES;
#endif
} // namespace

Q_DECLARE_METATYPE(BellaOperationCase)

// MARK: - Test fixture

class TestFileOperations : public QObject
{
	Q_OBJECT
private slots:
	void copy_publishes_and_preserves_source();
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
	void native_trash_refusals_share_one_consent_and_undo();
	void native_trash_fallback_declined_keeps_originals();
	void native_trash_fallback_rechecks_changed_original();
	void native_trash_failure_keeps_changed_source_visible();
	void native_trash_ambiguous_result_never_offers_fallback_data();
	void native_trash_ambiguous_result_never_offers_fallback();
	void native_trash_cancellation_never_offers_fallback();
	void native_trash_success_excluded_from_fallback_batch();
	void native_trash_fallback_crash_resume_data();
	void native_trash_fallback_crash_resume();
	void native_trash_fallback_batch_consent_survives_partial_completion();
	void undo_copy_native_refusal_uses_consented_fallback();
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

	void move_copies_every_file_before_removal();
	void changed_completed_destination_retains_original_data();
	void changed_completed_destination_retains_original();
	void failed_copy_continues_and_blocks_all_original_removal();
	void explicit_skip_survives_disappearing_conflict();
	void removal_crash_boundaries_resume();
	void undo_copy_is_gated_and_claims_forward();
	void undo_partial_move_restores_only_completed_changes();
	void undo_interrupted_copy_and_changed_result();
	void undo_move_resumes_after_publication_data();
	void undo_move_resumes_after_publication();
	void local_trash_and_undo_roundtrip();

	void keep_both_retains_names_and_handles_late_race();
	void skip_never_overwrites();
	void replace_is_rejected();
	void source_edit_is_retained();
	void cancellation_preserves_replacement();
	void failed_cleanup_remains_journalled();
	void cancel_during_retirement_restores_original_data();
	void cancel_during_retirement_restores_original();
	void blocked_restore_survives_dismissal_and_later_job_data();
	void blocked_restore_survives_dismissal_and_later_job();
	void restoration_crash_boundaries_are_idempotent_data();
	void restoration_crash_boundaries_are_idempotent();
	void completed_transfers_remove_private_directories_data();
	void completed_transfers_remove_private_directories();
	void cleanup_crash_boundaries_are_recoverable_data();
	void cleanup_crash_boundaries_are_recoverable();
	void cleanup_retains_replaced_partial_and_unrelated_files();
	void cleanup_retains_partial_when_original_changes();
	void undo_settles_interrupted_retirement_cleanup_data();
	void undo_settles_interrupted_retirement_cleanup();
	void journal_failure_stops_publication();
	void missing_journal_prevents_relocation();
	void repeated_resume_continues_same_journal();
	void source_retention_is_explicit();
	void same_volume_move_preserves_identity();
	void rename_group_conflict_skips_every_member();
	void rename_respects_folder_capacity_data();
	void rename_respects_folder_capacity();
	void rename_failure_stops_group();
	void different_source_on_resume_is_refused();
	void corrupt_journal_is_preserved();
	void trash_preserves_original_and_location();
	void partial_group_failure_keeps_every_file_recorded();
	void same_volume_keep_both_handles_late_conflict();
	void header_checks_remain_independent_of_selected_master_data();
	void header_checks_remain_independent_of_selected_master();
	void bella_handoff_checks_copy_move_delete_data();
	void bella_handoff_checks_copy_move_delete();
	void operation_scan_receipt_is_required_data();
	void operation_scan_receipt_is_required();
	void changed_scan_header_cannot_become_unknown();
	void conflicting_scan_header_is_refused_without_history();
	void journal_volume_paths_survive_two_resolutions();
	void mismatched_volume_is_never_session_matched();
	void second_runner_cannot_change_files();
	void facade_refuses_second_job_without_cancelling_first();
	void rebalance_cancel_before_queued_dispatch_keeps_source();
	void rebalance_replacement_ignores_queued_preparation_data();
	void rebalance_replacement_ignores_queued_preparation();
	void rebalance_rejected_preparation_aborts_data();
	void rebalance_rejected_preparation_aborts();
	void bella_rebalance_rescan_refuses_whole_plan_data();
	void bella_rebalance_rescan_refuses_whole_plan();
	void bella_rebalance_live_checks_and_recovery_data();
	void bella_rebalance_live_checks_and_recovery();
	void rebalance_empty_plan_finishes_without_aborting();
	void invalid_mxf_claims_are_refused_by_adapter();
	void unsupported_directory_flush_preserves_originals();
	void directory_io_failure_stops_copy();
	void unsupported_relocation_keeps_media_and_databases();
	void directory_creation_propagates_durability();
	void selected_duplicates_keep_both();
	void regenerated_database_is_retired_on_resume();
	void torn_tail_retains_valid_prefix();
	void abrupt_process_exit_is_recoverable_data();
	void abrupt_process_exit_is_recoverable();
#ifdef Q_OS_WIN
	void encrypted_windows_copy_is_refused();
#endif
	void rebalance_late_conflict_stops_remaining_members();
};
// MARK: - Copy publication and advisory planning

void TestFileOperations::copy_publishes_and_preserves_source()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	auto totals = runner.run(f.request(), f.journals);
	QVERIFY2(totals.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.src), f.bytes);
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records[0].entries[0].step, OperationJournal::Step::Done);
	QVERIFY(records[0].entries[0].landed.valid());
	QCOMPARE(records[0].entries[0].originalSource, f.src);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Copy complete: 1 file copied to %1.").arg(f.dest)});
}
void TestFileOperations::advisory_copy_move_assessment_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<bool>("firstCanRelocate");
	QTest::addColumn<bool>("secondCanRelocate");
	QTest::addColumn<bool>("skipSecond");
	QTest::addColumn<bool>("forceCopy");
	QTest::addColumn<bool>("copyThenRemove");
	QTest::addColumn<qint64>("requiredCopyBytes");
	QTest::addColumn<int>("relocationProbeCount");
	QTest::newRow("copy") << int(OperationKind::Copy) << true << true << false << false << false << qint64(300) << 0;
	QTest::newRow("same-volume-move") << int(OperationKind::Move) << true << true << false << false << false << qint64(0) << 2;
	QTest::newRow("mixed-volume-move") << int(OperationKind::Move) << true << false << false << false << true << qint64(300) << 2;
	QTest::newRow("first-file-needs-copy") << int(OperationKind::Move) << false << true << false << false << true << qint64(300) << 1;
	QTest::newRow("skip-cross-volume") << int(OperationKind::Move) << true << false << true << false << false << qint64(0) << 1;
	QTest::newRow("copy-excludes-skip") << int(OperationKind::Copy) << true << false << true << false << false << qint64(100) << 0;
	QTest::newRow("forced-copy-move") << int(OperationKind::Move) << true << true << false << true << true << qint64(300) << 0;
	QTest::newRow("delete") << int(OperationKind::Delete) << true << false << false << false << false << qint64(0) << 0;
}
void TestFileOperations::advisory_copy_move_assessment()
{
	QFETCH(int, kind);
	QFETCH(bool, firstCanRelocate);
	QFETCH(bool, secondCanRelocate);
	QFETCH(bool, skipSecond);
	QFETCH(bool, forceCopy);
	QFETCH(bool, copyThenRemove);
	QFETCH(qint64, requiredCopyBytes);
	QFETCH(int, relocationProbeCount);
	OperationRequest request;
	request.kind = static_cast<OperationKind>(kind);
	request.destRoot = "/destination";
	OperationItem first;
	first.src = "/source/first.mxf";
	first.name = "first.mxf";
	first.bytes = 100;
	auto second = first;
	second.src = "/other/second.mxf";
	second.name = "second.mxf";
	second.bytes = 200;
	if (skipSecond)
		second.policy = "skip";
	request.items = {first, second};
	QStringList probedSources, probedDestinations, identityProbes;
	const auto canRelocate = [&](const QString &source, const QString &destination)
	{
		probedSources.append(source);
		probedDestinations.append(destination);
		return source == first.src ? firstCanRelocate : secondCanRelocate;
	};
	const auto sameFile = [&](const QString &source, const QString &)
	{
		identityProbes.append(source);
		return false;
	};
	const auto assessment = OperationPlan::assessCopyMove(request, canRelocate, forceCopy, sameFile);
	QCOMPARE(assessment.copyThenRemove, copyThenRemove);
	QCOMPARE(assessment.requiredCopyBytes, requiredCopyBytes);
	QCOMPARE(probedSources.size(), relocationProbeCount);
	if (!probedDestinations.isEmpty())
		QCOMPARE(probedDestinations.first(), QString("/destination/first.mxf"));
	if (skipSecond)
		QVERIFY(!probedSources.contains(second.src));
	// Choosing the copy route must not stop identity checks on later items.
	const bool copyOrMove = request.kind == OperationKind::Copy || request.kind == OperationKind::Move;
	QCOMPARE(identityProbes.size(), copyOrMove ? (skipSecond ? 1 : 2) : 0);
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
	OperationRequest request;
	OperationItem item;
	item.bytes = (std::numeric_limits<qint64>::max)();
	request.items = {item, item};
	QCOMPARE(OperationPlan::assessCopyMove(request).requiredCopyBytes, item.bytes);
}
void TestFileOperations::move_rechecks_advisory_strategy()
{
	Fixture f;
	auto request = f.request(OperationKind::Move);
	QVERIFY(!OperationPlan::assessCopyMove(request).copyThenRemove);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true; // Execution has stricter capability evidence than the preview.
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto saved = OperationJournal::scan(f.journals);
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
	const auto existingStamp = OperationFile::inspect(existing);
	auto request = f.request(OperationKind::Move);
	auto noEffect = request.items[0];
	noEffect.src = existing;
	noEffect.name = "already.bin";
	noEffect.bytes = existingBytes.size();
	request.items.prepend(noEffect);
	const auto assessment = OperationPlan::assessCopyMove(request,
														  [](const QString &, const QString &)
														  { return false; });
	QVERIFY(assessment.copyThenRemove);
	QCOMPARE(assessment.requiredCopyBytes, qint64(f.bytes.size()));
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true; // Exercise the mixed-volume Move strategy on disposable local files.
	if (resume)
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{
			if (stage == "no-effect")
				cancel = true;
		};
	auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.unchanged, 1);
	QCOMPARE(totals.skipped, 0);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.needsAttention, 0);
	auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const QString forwardPath = records[0].path;
	const auto &savedNoEffect = records[0].entries[0];
	QCOMPARE(savedNoEffect.step, OperationJournal::Step::NoEffect);
	QVERIFY(savedNoEffect.complete());
	QVERIFY(!savedNoEffect.explicitSkip);
	QVERIFY(!savedNoEffect.sourceRemoved);
	QVERIFY(savedNoEffect.mechanism.isEmpty());
	QVERIFY(savedNoEffect.source.unchanged(savedNoEffect.landed));
	QCOMPARE(savedNoEffect.attempts, 0);
	QCOMPARE(sink.results[0].state, OperationResult::State::NoEffect);
	if (resume)
	{
		QVERIFY(totals.cancelled);
		QCOMPARE(records[0].entries[1].attempts, 0);
		QCOMPARE(get(f.src), f.bytes);
		cancel = false;
		runner.hooks = {};
		OperationRequest continuation;
		continuation.resumeJournalPath = forwardPath;
		totals = runner.run(continuation, f.journals);
	}
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.retained, 0);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.needsAttention, 0);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QVERIFY(existingStamp.unchanged(OperationFile::inspect(existing)));
	const auto forward = OperationJournal::readOne(forwardPath);
	QVERIFY(forward);
	QVERIFY(forward->copiesComplete);
	QCOMPARE(forward->entries[0].step, OperationJournal::Step::NoEffect);
	QCOMPARE(forward->entries[1].step, OperationJournal::Step::SourceRemoved);
	QVERIFY(OperationRecovery::readSummary(f.journals).resumable.isEmpty());
	QCOMPARE(copyMoveSummaries(sink).size(), resume ? 2 : 1);
	QCOMPARE(copyMoveSummaries(sink).last(),
			 QStringLiteral("Move complete: 1 file moved to %1; 1 already at destination.").arg(f.dest));
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forwardPath;
	const auto undone = runner.run(undo, f.journals);
	QCOMPARE(undone.succeeded, 1);
	QCOMPARE(undone.needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(existingStamp.unchanged(OperationFile::inspect(existing)));
	QCOMPARE(copyMoveSummaries(sink).size(), resume ? 2 : 1);
}
void TestFileOperations::already_at_destination_copy_is_not_undone()
{
	Fixture f;
	const QString existing = f.dest + "/already.bin";
	put(existing, "already here");
	const auto before = OperationFile::inspect(existing);
	auto request = f.request();
	auto noEffect = request.items[0];
	noEffect.src = existing;
	noEffect.name = "already.bin";
	noEffect.bytes = before.size;
	request.items.prepend(noEffect);
	QCOMPARE(OperationPlan::assessCopyMove(request).requiredCopyBytes, qint64(f.bytes.size()));
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.unchanged, 1);
	const auto forward = OperationJournal::scan(f.journals)[0];
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	QCOMPARE(runner.run(undo, f.journals).succeeded, 1);
	QVERIFY(before.unchanged(OperationFile::inspect(existing)));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	for (const auto &record : OperationJournal::scan(f.journals))
		if (record.request.kind == OperationKind::Undo)
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
													   [](const QString &, const QString &)
													   { return true; });
	QCOMPARE(preview.requiredCopyBytes, qint64(0));
	put(f.dest + "/clip.bin", "a different existing file");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(totals.unchanged, 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("a different existing file"));
}
void TestFileOperations::already_at_destination_only_job()
{
	for (const auto kind : {OperationKind::Copy, OperationKind::Move})
	{
		Fixture f;
		auto request = f.request(kind);
		request.destRoot = QFileInfo(f.src).absolutePath();
		QVERIFY(OperationPlan::alreadyAtDestination(f.src, f.src));
		QCOMPARE(OperationPlan::assessCopyMove(request).requiredCopyBytes, qint64(0));
		Sink sink;
		std::atomic<bool> cancel{false};
		OperationRunner runner(sink, cancel);
		const auto totals = runner.run(request, f.journals);
		QCOMPARE(totals.unchanged, 1);
		QCOMPARE(totals.succeeded, 0);
		QCOMPARE(totals.skipped, 0);
		QCOMPARE(totals.failed, 0);
		QVERIFY(!OperationJournal::latestUndoable(f.journals));
		QVERIFY(OperationRecovery::readSummary(f.journals).resumable.isEmpty());
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(copyMoveSummaries(sink), QStringList{
											  kind == OperationKind::Copy ? QStringLiteral("Copy complete: 0 files copied to %1; 1 already at destination.").arg(request.destRoot)
																		  : QStringLiteral("Move complete: 0 files moved to %1; 1 already at destination.").arg(request.destRoot)});
	}
}
void TestFileOperations::undo_already_restored_object_finishes_without_changes()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	QCOMPARE(runner.run(f.request(OperationKind::Move), f.journals).succeeded, 1);
	const auto forward = OperationJournal::scan(f.journals)[0];
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "copying")
			cancel = true;
	};
	QVERIFY(runner.run(undo, f.journals).cancelled);
	const auto pending = OperationJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].request.kind, OperationKind::Undo);
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
	OperationRequest continuation;
	continuation.resumeJournalPath = pending[0].path;
	const auto totals = runner.run(continuation, f.journals);
	QCOMPARE(totals.unchanged, 1);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(totals.failed, 0);
	QVERIFY(OperationRecovery::readSummary(f.journals).resumable.isEmpty());
	QVERIFY(!OperationJournal::latestUndoable(f.journals));
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(copied), f.bytes);
	const auto finished = OperationJournal::readOne(pending[0].path);
	QVERIFY(finished);
	QCOMPARE(finished->entries[0].step, OperationJournal::Step::NoEffect);
	QCOMPARE(finished->request.undoOf, forward.path);
}
// MARK: - Conflict handling and source protection

void TestFileOperations::keep_both_retains_names_and_handles_late_race()
{
	Fixture f;
	put(f.dest + "/clip.bin", "original");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	bool raced = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "publishing" && !raced)
		{
			put(e.dst, "other writer");
			raced = true;
		}
	};
	const auto totals = runner.run(f.request(OperationKind::Copy, "keepboth"), f.journals);
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
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OperationKind::Copy, "skip"), f.journals).skipped, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("original"));
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Copy complete: 0 files copied to %1; 1 skipped.").arg(f.dest)});
}
void TestFileOperations::replace_is_rejected()
{
	Fixture f;
	put(f.dest + "/clip.bin", "original");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OperationKind::Copy, "replace"), f.journals);
	QCOMPARE(t.succeeded, 0);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("original"));
}
void TestFileOperations::source_edit_is_retained()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	bool edited = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "copy-ready")
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
	const auto t = runner.run(f.request(OperationKind::Move), f.journals);
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
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "copy-ready")
		{
			put(e.dst, "other writer");
			cancel = true;
		}
	};
	auto t = runner.run(f.request(), f.journals);
	QVERIFY(t.cancelled);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("other writer"));
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Copy cancelled: 0 files copied to %1; 1 not completed.").arg(f.dest)});
}
void TestFileOperations::failed_cleanup_remains_journalled()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &point)
	{ return point == "cleanup"; };
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "copy-chunk")
			cancel = true;
	};
	runner.run(f.request(), f.journals);
	const auto rec = OperationJournal::scan(f.journals).first();
	QVERIFY(!rec.entries[0].artifacts.isEmpty());
	QVERIFY(QFile::exists(rec.entries[0].artifacts[0]));
	QVERIFY(!rec.entries[0].cleanup.isEmpty());
	QString error;
	QVERIFY(OperationJournal::dismiss(rec.path, error));
	auto a = OperationRecovery::run(f.journals);
	auto b = OperationRecovery::run(f.journals);
	QVERIFY2(a.opsFlagged == 0, qPrintable(a.message()));
	QVERIFY2(b.opsFlagged == 0, qPrintable(b.message()));
	QVERIFY(privateDirectories(f.root).isEmpty());
	const auto recovered = OperationJournal::readOne(rec.path);
	QVERIFY(recovered && recovered->dismissed);
	QVERIFY(recovered->entries[0].cleanup.isEmpty());
	QVERIFY(recovered->entries[0].artifacts.isEmpty());
	QVERIFY(QFile::exists(rec.path));
}

// MARK: - Original restoration

void TestFileOperations::cancel_during_retirement_restores_original_data()
{
	QTest::addColumn<QString>("boundary");
	for (const auto *point : {"before-source-retirement", "source-retired"})
		QTest::newRow(point) << QString(point);
}
void TestFileOperations::cancel_during_retirement_restores_original()
{
	QFETCH(QString, boundary);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{ if (point == boundary) cancel = true; };
	auto request = f.request(OperationKind::Move);
	const auto totals = runner.run(request, f.journals);
	QVERIFY(totals.cancelled);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	const auto record = OperationJournal::scan(f.journals).first();
	QCOMPARE(record.entries[0].step, OperationJournal::Step::SourceRestored);
	QVERIFY(!record.entries[0].sourceRemoved);
	QVERIFY(record.entries[0].cleanup.isEmpty());
	QVERIFY(privateDirectories(f.root).isEmpty());
	QCOMPARE(sink.results.last().state, OperationResult::State::OriginalRestored);
	QVERIFY(!sink.results.last().sourceRemoved);
	QVERIFY(OperationRecovery::readSummary(f.journals).restorable.isEmpty());
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move cancelled: 0 files moved to %1; 1 not completed.").arg(f.dest)});
	cancel = false;
	runner.hooks = {};
	OperationRequest resume;
	resume.resumeJournalPath = record.path;
	QCOMPARE(runner.run(resume, f.journals).needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes); // Resume must not retire this original again.
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(copyMoveSummaries(sink).last(),
			 QStringLiteral("Move incomplete: 0 files moved to %1; 1 not completed.").arg(f.dest));
}

void TestFileOperations::blocked_restore_survives_dismissal_and_later_job_data()
{
	QTest::addColumn<bool>("removeDestination");
	QTest::newRow("destination-missing") << true;
	QTest::newRow("destination-replaced") << false;
}
void TestFileOperations::blocked_restore_survives_dismissal_and_later_job()
{
	QFETCH(bool, removeDestination);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{
		if (point == "source-retired")
		{
			put(f.src, "other writer");
			cancel = true;
		}
	};
	QVERIFY(runner.run(f.request(OperationKind::Move), f.journals).needsAttention > 0);
	const auto record = OperationJournal::scan(f.journals).first();
	QCOMPARE(record.entries[0].step, OperationJournal::Step::RestoringSource);
	QCOMPARE(get(record.entries[0].retirement), f.bytes);
	QCOMPARE(get(f.src), QByteArray("other writer"));
	QString error;
	QVERIFY(OperationJournal::dismiss(record.path, error));
	cancel = false;
	runner.hooks = {};
	auto later = f.request();
	later.items[0].src = f.root + "/later.bin";
	later.items[0].name = "later.bin";
	put(later.items[0].src, f.bytes);
	QCOMPARE(runner.run(later, f.journals).succeeded, 1);
	QCOMPARE(OperationRecovery::readSummary(f.journals).restorable.size(), 1);
	QVERIFY(QFile::remove(f.src)); // Remove only the disposable replacement fixture.
	QVERIFY(QFile::remove(f.dest + "/clip.bin"));
	if (!removeDestination)
		put(f.dest + "/clip.bin", "changed destination");
	OperationRequest restore;
	restore.restoreJournalPath = record.path;
	sink.messages.clear();
	const auto restored = runner.run(restore, f.journals);
	QVERIFY2(restored.needsAttention == 0, qPrintable(sink.messages.join('\n')));
	QCOMPARE(restored.succeeded, 1);
	QCOMPARE(sink.results.last().state, OperationResult::State::OriginalRestored);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), removeDestination ? QByteArray() : QByteArray("changed destination"));
	QCOMPARE(get(f.dest + "/later.bin"), f.bytes);
	QVERIFY(OperationJournal::readOne(record.path)->dismissed);
	QVERIFY(OperationRecovery::readSummary(f.journals).restorable.isEmpty());
	QCOMPARE(runner.run(restore, f.journals).needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(copyMoveSummaries(sink).isEmpty());
}

void TestFileOperations::restoration_crash_boundaries_are_idempotent_data()
{
	QTest::addColumn<QString>("boundary");
	for (const auto *point : {"restoring-source", "before-source-restore", "source-restored-on-disk", "source-restored"})
		QTest::newRow(point) << QString(point);
}
void TestFileOperations::restoration_crash_boundaries_are_idempotent()
{
	QFETCH(QString, boundary);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{
		if (point == "source-retired")
			cancel = true;
		if (point == boundary)
			throw std::runtime_error("restoration crash");
	};
	QVERIFY(runner.run(f.request(OperationKind::Move), f.journals).needsAttention > 0);
	const auto record = OperationJournal::scan(f.journals).first();
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	const auto recovery = OperationRecovery::run(f.journals);
	QVERIFY(recovery.undoCandidate);
	QCOMPARE(recovery.undoCandidate->path, record.path);
	const bool returnedBeforeRecovery = boundary == "source-restored-on-disk" || boundary == "source-restored";
	QCOMPARE(recovery.restorable.size(), returnedBeforeRecovery ? 0 : 1);
	if (returnedBeforeRecovery)
		QCOMPARE(recovery.undoCandidate->entries[0].step, OperationJournal::Step::SourceRestored);
	cancel = false;
	runner.hooks = {};
	OperationRequest restore;
	restore.restoreJournalPath = record.path;
	QCOMPARE(runner.run(restore, f.journals).needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(OperationJournal::readOne(record.path)->entries[0].step, OperationJournal::Step::SourceRestored);
	QVERIFY(OperationRecovery::readSummary(f.journals).restorable.isEmpty());
	QVERIFY(privateDirectories(f.root).isEmpty());
	OperationRequest resume;
	resume.resumeJournalPath = record.path;
	QCOMPARE(runner.run(resume, f.journals).needsAttention, 0);
	QCOMPARE(get(f.src), f.bytes);
}

// MARK: - Private directory cleanup

void TestFileOperations::completed_transfers_remove_private_directories_data()
{
	QTest::addColumn<bool>("move");
	for (bool move : {false, true})
		QTest::newRow(move ? "move" : "copy") << move;
}
void TestFileOperations::completed_transfers_remove_private_directories()
{
	QFETCH(bool, move);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	auto request = f.request(move ? OperationKind::Move : OperationKind::Copy);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(QFile::exists(f.src), !move);
	QVERIFY(privateDirectories(f.root).isEmpty());
	const auto record = OperationJournal::scan(f.journals).first();
	QVERIFY(record.entries[0].cleanup.isEmpty());
	QVERIFY(record.entries[0].artifacts.isEmpty());
	QCOMPARE(OperationRecovery::run(f.journals).opsFlagged, 0);
}

void TestFileOperations::cleanup_crash_boundaries_are_recoverable_data()
{
	QTest::addColumn<QString>("boundary");
	QTest::addColumn<bool>("cancelCopy");
	for (const auto *point : {"published", "done", "before-directory-cleanup", "directory-cleaned"})
		QTest::newRow(point) << QString(point) << false;
	for (const auto *point : {"before-partial-cleanup", "partial-cleaned"})
		QTest::newRow(point) << QString(point) << true;
}
void TestFileOperations::cleanup_crash_boundaries_are_recoverable()
{
	QFETCH(QString, boundary);
	QFETCH(bool, cancelCopy);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{
		if (cancelCopy && point == "copy-chunk")
			cancel = true;
		if (point == boundary)
			throw std::runtime_error("cleanup crash");
	};
	QVERIFY(runner.run(f.request(), f.journals).needsAttention > 0);
	const auto record = OperationJournal::scan(f.journals).first();
	QCOMPARE(get(f.src), f.bytes);
	if (!cancelCopy)
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	for (int repeat = 0; repeat != 2; ++repeat)
	{
		const auto recovery = OperationRecovery::run(f.journals);
		QVERIFY2(recovery.opsFlagged == 0, qPrintable(recovery.message()));
		QVERIFY(privateDirectories(f.root).isEmpty());
		const auto saved = OperationJournal::readOne(record.path);
		QVERIFY(saved && saved->entries[0].cleanup.isEmpty());
		QVERIFY(saved->entries[0].artifacts.isEmpty());
		QCOMPARE(get(f.src), f.bytes);
	}
}

void TestFileOperations::cleanup_retains_replaced_partial_and_unrelated_files()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &point)
	{ return point == "cleanup"; };
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{ if (point == "copy-chunk") cancel = true; };
	QVERIFY(runner.run(f.request(), f.journals).cancelled);
	const auto record = OperationJournal::scan(f.journals).first();
	const auto pending = record.entries[0].cleanup.first();
	QVERIFY(QFile::rename(pending.file, pending.directory + "/saved-partial.bin"));
	put(pending.file, "replacement");
	put(pending.directory + "/unrelated.bin", "do not remove");
	const QString unrecorded = f.dest + "/.mediamuster-stage-00000000-0000-4000-8000-000000000001";
	put(unrecorded + "/payload.partial", "unrecorded");
	const auto recovery = OperationRecovery::run(f.journals);
	QVERIFY(recovery.opsFlagged > 0);
	QCOMPARE(get(pending.file), QByteArray("replacement"));
	QCOMPARE(get(pending.directory + "/unrelated.bin"), QByteArray("do not remove"));
	QCOMPARE(get(unrecorded + "/payload.partial"), QByteArray("unrecorded"));
	QVERIFY(!OperationJournal::readOne(record.path)->entries[0].cleanup.isEmpty());
	QCOMPARE(get(f.src), f.bytes);
}

void TestFileOperations::cleanup_retains_partial_when_original_changes()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &point)
	{ return point == "cleanup"; };
	runner.hooks.checkpoint = [&](const QString &point, const auto &)
	{ if (point == "copy-chunk") cancel = true; };
	QVERIFY(runner.run(f.request(), f.journals).cancelled);
	auto record = OperationJournal::scan(f.journals).first();
	auto entry = record.entries[0];
	const QString partial = entry.cleanup.first().file;
	QString error;
	OperationJournal journal;
	QVERIFY(journal.resume(record, error));
	OperationRunner::Hooks hooks;
	bool moved = false;
	hooks.checkpoint = [&](const QString &point, const auto &)
	{
		if (point == "before-partial-cleanup")
			moved = QFile::rename(f.src, f.src + ".outside");
	};
	const bool cleaned = OperationRunner::cleanup(journal, entry, error, &hooks);
	// Windows can hold the surviving original against the rename; Mac must
	// detect that it changed after the confirmation and keep the partial.
	if (moved)
	{
		QVERIFY(!cleaned);
		QVERIFY(QFile::exists(partial));
		QVERIFY(!entry.cleanup.isEmpty());
	}
	else
		QVERIFY2(cleaned, qPrintable(error));
	QCOMPARE(get(moved ? f.src + ".outside" : f.src), f.bytes);
}

void TestFileOperations::undo_settles_interrupted_retirement_cleanup_data()
{
	QTest::addColumn<QString>("boundary");
	QTest::newRow("retired-original") << QStringLiteral("source-retired");
	QTest::newRow("removed-original") << QStringLiteral("source-unlinked");
}
void TestFileOperations::undo_settles_interrupted_retirement_cleanup()
{
	QFETCH(QString, boundary);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = interruptAt(boundary);
	QVERIFY(runner.run(f.request(OperationKind::Move), f.journals).needsAttention > 0);
	const auto forward = OperationJournal::scan(f.journals).first();
	runner.hooks = {};
	if (boundary == "source-unlinked")
	{
		OperationRequest restore;
		restore.restoreJournalPath = forward.path;
		QVERIFY(runner.run(restore, f.journals).needsAttention > 0);
		// An unsuccessful attempt to find a retained original must not prevent
		// Undo from recovering it from the unchanged destination copy.
	}
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	const auto undone = runner.run(undo, f.journals);
	QVERIFY2(undone.needsAttention == 0 && undone.failed == 0, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	const auto recovery = OperationRecovery::run(f.journals);
	QVERIFY2(recovery.opsFlagged == 0, qPrintable(recovery.message()));
	QVERIFY(privateDirectories(f.root).isEmpty());
	QVERIFY(OperationJournal::readOne(forward.path)->entries[0].cleanup.isEmpty());
	QVERIFY(!OperationJournal::readOne(forward.path)->undoPath.isEmpty());
	QVERIFY(OperationRecovery::readSummary(f.journals).restorable.isEmpty());
}
// MARK: - Journal failures and crash recovery

void TestFileOperations::journal_failure_stops_publication()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	bool failed = false;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "copy-ready")
			failed = true;
	};
	runner.hooks.fail = [&](const QString &point)
	{ return point == "journal" && failed; };
	const auto t = runner.run(f.request(), f.journals);
	QVERIFY(t.needsAttention > 0);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(QFile::exists(f.src));
	QCOMPARE(OperationJournal::scan(f.journals).size(), 1);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Copy incomplete: 0 files copied to %1; 1 not completed.").arg(f.dest)});
}
void TestFileOperations::missing_journal_prevents_relocation()
{
	Fixture f;
	put(f.journals, "block");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OperationKind::Move), f.journals);
	QCOMPARE(t.succeeded, 0);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move incomplete: 0 files moved to %1; 1 not completed.").arg(f.dest)});
}
void TestFileOperations::repeated_resume_continues_same_journal()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = interruptAt("copy-ready");
	runner.run(f.request(), f.journals);
	auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	OperationRequest resume;
	resume.resumeJournalPath = records[0].path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(OperationJournal::scan(f.journals).size(), 1);
	QCOMPARE(runner.run(resume, f.journals).succeeded, 0);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
}
void TestFileOperations::source_retention_is_explicit()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &s, const auto &)
	{
		if (s == "copies-complete")
			cancel = true;
	};
	auto t = runner.run(f.request(OperationKind::Move), f.journals);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(sink.results.last().state, OperationResult::State::SourceRetained);
	QVERIFY(!sink.results.last().sourceRemoved);
}
// MARK: - Relocation and rebalance groups

void TestFileOperations::same_volume_move_preserves_identity()
{
	Fixture f;
	const auto original = OperationFile::inspect(f.src);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto t = runner.run(f.request(OperationKind::Move), f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QVERIFY(original.sameObject(OperationFile::inspect(f.dest + "/clip.bin")));
	QVERIFY(!QFile::exists(f.src));
	QVERIFY(sink.results.last().sourceRemoved);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move complete: 1 file moved to %1.").arg(f.dest)});
}
void TestFileOperations::rename_group_conflict_skips_every_member()
{
	Fixture f;
	put(f.root + "/source/audio.bin", "audio");
	put(f.dest + "/clip.bin", "block");
	auto req = f.request(OperationKind::Rename);
	req.items[0].renameDst = f.dest + "/clip.bin";
	req.items[0].groupKey = "clip";
	OperationItem a;
	a.src = f.root + "/source/audio.bin";
	a.name = "audio.bin";
	a.bytes = 5;
	a.renameDst = f.dest + "/audio.bin";
	a.groupKey = "clip";
	req.items.append(a);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(req, f.journals).skipped, 2);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(QFile::exists(a.src));
	QCOMPARE(sink.messages.size(), 2);
	QCOMPARE(sink.messages.filter(QStringLiteral("Rebalance group skipped")).size(), 1);
	QCOMPARE(sink.messages.last(), QStringLiteral("Rebalance complete: 0 files moved; 2 skipped."));
}
void TestFileOperations::rename_respects_folder_capacity_data()
{
	QTest::addColumn<int>("existingFiles");
	QTest::addColumn<bool>("accepted");
	QTest::newRow("final-count-5000") << 4999 << true;
	QTest::newRow("final-count-5001") << 5000 << false;
}
void TestFileOperations::rename_respects_folder_capacity()
{
	QFETCH(int, existingFiles);
	QFETCH(bool, accepted);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const QString base = OperationJournal::canonicalPath(temp.path());
	const QString root = base + "/Avid MediaFiles/MXF";
	const QString sourceFolder = root + "/Ingest1.1";
	const QString destinationFolder = root + "/Ingest1.2";
	const QByteArray media("media"), database("database");
	put(sourceFolder + "/clip.mxf", media);
	put(sourceFolder + "/msmMMOB.mdb", database);
	put(destinationFolder + "/msmMMOB.mdb", database);
	for (int n = 0; n < existingFiles; ++n)
	{
		QFile filler(destinationFolder + QStringLiteral("/filler-%1.mxf").arg(n));
		QVERIFY(filler.open(QIODevice::WriteOnly));
	}
	OperationRequest request;
	request.kind = OperationKind::Rename;
	request.diagnosticTrashRoot = base + "/_MediaMuster_Trash";
	OperationItem item;
	item.src = sourceFolder + "/clip.mxf";
	item.name = "clip.mxf";
	item.bytes = media.size();
	item.renameDst = destinationFolder + "/clip.mxf";
	item.groupKey = "relatives";
	request.items.append(item);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	const auto totals = runner.run(request, base + "/journals");
	QCOMPARE(totals.succeeded, accepted ? 1 : 0);
	QCOMPARE(totals.skipped, accepted ? 0 : 1);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(sink.results.size(), 1);
	QCOMPARE(sink.results.first().state, accepted ? OperationResult::State::Completed : OperationResult::State::Skipped);
	QCOMPARE(sink.results.first().sourceRemoved, accepted);
	QCOMPARE(get(accepted ? item.renameDst : item.src), media);
	QVERIFY(!QFile::exists(accepted ? item.src : item.renameDst));
	QCOMPARE(QDir(destinationFolder).entryList({"*.mxf"}, QDir::Files).size(), 5000);
	const auto records = OperationJournal::scan(base + "/journals");
	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().entries.first().step,
			 accepted ? OperationJournal::Step::Done : OperationJournal::Step::Skipped);
	if (!accepted)
	{
		QCOMPARE(get(sourceFolder + "/msmMMOB.mdb"), database);
		QCOMPARE(get(destinationFolder + "/msmMMOB.mdb"), database);
		QCOMPARE(records.first().entries.size(), 1); // No database retirement before a capacity skip.
	}
}
void TestFileOperations::rename_failure_stops_group()
{
	Fixture f;
	auto req = f.request(OperationKind::Rename);
	req.items[0].renameDst = f.dest + "/clip.bin";
	req.items[0].groupKey = "clip";
	OperationItem a;
	a.src = f.root + "/source/audio.bin";
	a.name = "audio.bin";
	a.bytes = 5;
	a.renameDst = f.dest + "/audio.bin";
	a.groupKey = "clip";
	put(a.src, "audio");
	req.items.append(a);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.fail = [](const QString &s)
	{ return s == "relocate"; };
	QCOMPARE(runner.run(req, f.journals).failed, 1);
	QVERIFY(QFile::exists(f.src));
	QVERIFY(QFile::exists(a.src));
}
void TestFileOperations::different_source_on_resume_is_refused()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = interruptAt("copying");
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
	OperationRunner runner(sink, cancel);
	runner.run(f.request(), f.journals);
	const auto path = OperationJournal::scan(f.journals)[0].path;
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
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot = f.root + "/_MediaMuster_Trash";
	const auto before = OperationFile::inspect(f.src);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto e = OperationJournal::scan(f.journals)[0].entries[0];
	QVERIFY(e.dst.startsWith(request.diagnosticTrashRoot + '/'));
	QVERIFY(before.sameObject(OperationFile::inspect(e.dst)));
	QCOMPARE(get(e.dst), f.bytes);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(e.originalSource, f.src);
	QVERIFY(!e.originalVolume.rootPath.isEmpty());
	QVERIFY(!e.originalRelativePath.isEmpty());
}
void TestFileOperations::partial_group_failure_keeps_every_file_recorded()
{
	Fixture f;
	auto request = f.request(OperationKind::Rename);
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
	OperationRunner runner(sink, cancel);
	int attempts = 0;
	runner.hooks.fail = [&](const QString &point)
	{ return point == "relocate" && ++attempts == 2; };
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(other.src), QByteArray("audio"));
	const auto record = OperationJournal::scan(f.journals)[0];
	QCOMPARE(record.entries[0].step, OperationJournal::Step::Done);
	QCOMPARE(record.entries[1].step, OperationJournal::Step::Failed);
	QVERIFY(QFile::exists(record.path));
	OperationRequest resume;
	resume.resumeJournalPath = record.path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(get(other.renameDst), QByteArray("audio"));
	QCOMPARE(OperationJournal::scan(f.journals).size(), 1);
}
void TestFileOperations::same_volume_keep_both_handles_late_conflict()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	bool raced = false;
	runner.hooks.checkpoint = [&](const QString &point, const auto &e)
	{
		if (point == "relocating" && !raced)
		{
			put(e.dst, "other");
			raced = true;
		}
	};
	QCOMPARE(runner.run(f.request(OperationKind::Move, "keepboth"), f.journals).succeeded, 1);
	QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("other"));
	QCOMPARE(get(f.dest + "/clip (2).bin"), f.bytes);
	QVERIFY(!QFile::exists(f.src));
}
// MARK: - Scan identity handoff

void TestFileOperations::header_checks_remain_independent_of_selected_master_data()
{
	QTest::addColumn<QString>("selectedMasterMobId");
	QTest::newRow("selected-header") << QStringLiteral("header-a");
	QTest::newRow("selected-database") << QStringLiteral("database-only-master");
	QTest::newRow("unresolved-selection") << QString{};
}

void TestFileOperations::header_checks_remain_independent_of_selected_master()
{
	QFETCH(QString, selectedMasterMobId);
	MediaFileRecord file;
	file.mediaFilePath = QStringLiteral("/source/clip.mxf");
	file.masterMobId = selectedMasterMobId;
	file.scanStamp = {file.mediaFilePath, {}, {}, QStringLiteral("file-id"), selectedMasterMobId};
	file.operationScanReceipt = OperationScanReceipt{
		OperationHeaderIdentity{QStringLiteral("file-id"),
								{QStringLiteral("header-a"), QStringLiteral("header-b")},
								{}},
		{}};
	const auto item = OperationManager::itemsFromMediaFiles({file}, {}).front();
	QCOMPARE(item.masterMobId, selectedMasterMobId);
	QVERIFY(item.headerIdentity);
	QCOMPARE(item.headerIdentity->masterMobIds,
			 (QStringList{QStringLiteral("header-a"), QStringLiteral("header-b")}));
	QVERIFY(item.headerIdentity->unavailableReason.isEmpty());
}

void TestFileOperations::bella_handoff_checks_copy_move_delete_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<BellaOperationCase>("scenario");
	QTest::addColumn<bool>("resume");
	for (const auto kind : {OperationKind::Copy, OperationKind::Move, OperationKind::Delete})
	{
		const auto add = [kind](const char *label, BellaOperationCase scenario)
		{
			QTest::newRow(qPrintable(operationKindName(kind) + '-' + QLatin1String(label))) << int(kind) << scenario << false;
		};
		add("header-matches", BellaOperationCase::HeaderMatch);
		add("header-full-id-differs", BellaOperationCase::HeaderMismatch);
		add("database-matches", BellaOperationCase::DatabaseMatch);
		add("database-uid-organisation-differs", BellaOperationCase::DatabaseMismatch);
		add("physical-only", BellaOperationCase::PhysicalOnly);
		add("rescan-changed", BellaOperationCase::Changed);
		add("rescan-unconfirmed-presence", BellaOperationCase::PresenceUnconfirmed);
	}
	QTest::newRow("delete-resume-with-database-id") << int(OperationKind::Delete) << BellaOperationCase::DatabaseMatch << true;
}

void TestFileOperations::bella_handoff_checks_copy_move_delete()
{
	QFETCH(int, kind);
	QFETCH(BellaOperationCase, scenario);
	QFETCH(bool, resume);
	const bool deleting = OperationKind(kind) == OperationKind::Delete;
	const bool fromHeader = scenario == BellaOperationCase::HeaderMatch || scenario == BellaOperationCase::HeaderMismatch;
	const bool fromDatabase = scenario == BellaOperationCase::DatabaseMatch || scenario == BellaOperationCase::DatabaseMismatch;
	const bool mismatch = scenario == BellaOperationCase::HeaderMismatch || scenario == BellaOperationCase::DatabaseMismatch;
	const bool rescan = scenario == BellaOperationCase::Changed || scenario == BellaOperationCase::PresenceUnconfirmed;
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = OperationJournal::canonicalPath(temporary.path());
	QVERIFY(!root.isEmpty());
	const auto path = root + QStringLiteral("/source/OMFI MediaFiles/clip.omf");
	const auto destination = root + QStringLiteral("/destination");
	const auto trashRoot = root + QStringLiteral("/_MediaMuster_Trash");
	if (!deleting)
		QVERIFY(QDir().mkpath(destination));

	Bella::ScanEntry entry;
	entry.path = path;
	entry.omfEra = true;
	QString selectedFileId, expectedFileId, expectedMasterId, refusal;
	QByteArray payload("No media identity established");
	if (fromHeader || fromDatabase)
	{
		const auto stored = QByteArray::fromHex(fromHeader
													? "060a2b340101010501010f10130000000102030405060708191a1b1c1d1e1f20"
													: "07000000443322110b000000");
		expectedFileId = fromHeader
							 ? QStringLiteral("060a2b3401010105.01010f1013000000.0102030405060708.191a1b1c1d1e1f20")
							 : QStringLiteral("omf:07000000443322110b000000");
		const auto layout = fromHeader ? Bella::MobIdLayout::Aaf32 : Bella::MobIdLayout::OmfUid12;
		const auto id = Bella::normalizeMobId(stored, layout, Bella::ByteOrder::LittleEndian);
		QVERIFY(std::holds_alternative<Bella::MobId>(id));
		const auto &verify = std::get<Bella::MobId>(id);
		entry.operationSafety = Bella::VerifyFileId{verify,
													fromHeader ? Bella::FileIdOrigin::Header : Bella::FileIdOrigin::Database};
		if (fromDatabase)
		{
			entry.metadata.fileMobId = verify;
			selectedFileId = expectedFileId;
		}

		// The selected editorial ID does not occur in the authored live header.
		const auto selectedMaster = Bella::normalizeMobId(
			QByteArray::fromHex("060a2b340101010501010f1013000000a1a2a3a4a5a6a7a8292a2b2c2d2e2f30"),
			Bella::MobIdLayout::Aaf32, Bella::ByteOrder::LittleEndian);
		QVERIFY(std::holds_alternative<Bella::MobId>(selectedMaster));
		entry.metadata.masterMobId = std::get<Bella::MobId>(selectedMaster);
		expectedMasterId = QStringLiteral("060a2b3401010105.01010f1013000000.a1a2a3a4a5a6a7a8.292a2b2c2d2e2f30");

		// Change a byte outside the eight material bytes for the 32-byte ID,
		// or only the organization word for the 12-byte UID.
		auto live = stored;
		if (mismatch)
			live[fromHeader ? 31 : 0] = fromHeader ? char(0x21) : char(8);
		payload = omfForOperationIdentity(live);
	}
	else if (rescan)
	{
		const bool changed = scenario == BellaOperationCase::Changed;
		entry.operationSafety = changed ? Bella::RescanReason::FileChanged : Bella::RescanReason::FilePresenceUnconfirmed;
		refusal = changed ? QStringLiteral("The media file changed during scanning. Rescan before proceeding.")
						  : QStringLiteral("The scan could not confirm that the media file was still present. Rescan before proceeding.");
	}
	if (fromHeader || scenario == BellaOperationCase::PhysicalOnly)
	{
		// In the header-mismatch case the live file agrees with this selected
		// metadata. Only the separately captured header instruction can refuse it.
		const auto selected = Bella::normalizeMobId(
			QByteArray::fromHex("060a2b340101010501010f10130000000102030405060708191a1b1c1d1e1f21"),
			Bella::MobIdLayout::Aaf32, Bella::ByteOrder::LittleEndian);
		QVERIFY(std::holds_alternative<Bella::MobId>(selected));
		entry.metadata.fileMobId = std::get<Bella::MobId>(selected);
		selectedFileId = QStringLiteral("060a2b3401010105.01010f1013000000.0102030405060708.191a1b1c1d1e1f21");
	}
	put(path, payload);
	if (fromHeader || fromDatabase)
	{
		// Validate the independently authored live ID so a wrong-ID test cannot
		// pass merely because the live reader returned an unreadable header.
		const auto identity = readHeaderIdentity(path, true);
		const auto liveId = !mismatch ? expectedFileId : fromHeader ? QStringLiteral("060a2b3401010105.01010f1013000000.0102030405060708.191a1b1c1d1e1f21")
																	: QStringLiteral("omf:08000000443322110b000000");
		QCOMPARE(identity.fileMobId, liveId);
	}
	const QFileInfo info(path);
	entry.sizeBytes = info.size();
	entry.modified = info.lastModified();
	entry.volumeIdentifier = VolumeIdentity::capture(path).identifier();
	QVERIFY(entry.modified.isValid());
	QVERIFY(!entry.volumeIdentifier.isEmpty());

	const auto row = mediaFileRecordFromScanEntry(entry);
	QVERIFY(row.operationScanReceipt);
	OperationRequest request;
	request.kind = OperationKind(kind);
	if (deleting)
		request.diagnosticTrashRoot = trashRoot;
	else
		request.destRoot = destination;
	request.items = OperationManager::itemsFromMediaFiles({row}, {});
	QCOMPARE(request.items.size(), qsizetype(1));
	const auto &item = request.items.front();
	QCOMPARE(item.scanPath, path);
	QCOMPARE(item.scanVolumeIdentifier, entry.volumeIdentifier);
	QCOMPARE(item.modifiedMs, entry.modified.toMSecsSinceEpoch());
	QCOMPARE(item.bytes, qint64(payload.size()));
	QCOMPARE(item.fileMobId, selectedFileId);
	QCOMPARE(item.masterMobId, expectedMasterId);
	QVERIFY(item.headerIdentity);
	QCOMPARE(item.headerIdentity->fileMobId, fromHeader ? expectedFileId : QString{});
	QCOMPARE(item.databaseFileMobIdToVerify, fromDatabase ? expectedFileId : QString{});
	QVERIFY(item.headerIdentity->masterMobIds.isEmpty());
	QCOMPARE(item.headerIdentity->unavailableReason, refusal);

	Sink sink;
	std::atomic_bool cancel{false};
	OperationRunner runner(sink, cancel);
	const auto journals = root + QStringLiteral("/journals");
	OperationRequest execution = request;
	if (resume)
	{
		runner.hooks.checkpoint = interruptAt(QStringLiteral("relocating"));
		QVERIFY(runner.run(request, journals).needsAttention > 0);
		QCOMPARE(get(path), payload);
		const auto recovery = OperationRecovery::run(journals);
		QCOMPARE(recovery.resumable.size(), qsizetype(1));
		const auto &pending = recovery.resumable.front();
		QCOMPARE(pending.total - pending.finished, 1);
		const auto saved = OperationJournal::readOne(pending.journalPath);
		QVERIFY(saved);
		QCOMPARE(saved->entries.front().item.databaseFileMobIdToVerify, expectedFileId);
		execution = {};
		execution.resumeJournalPath = pending.journalPath;
		runner.hooks.checkpoint = {};
	}
	const auto totals = runner.run(execution, journals);
	QVERIFY2(!sink.results.isEmpty(), qPrintable(sink.messages.join('\n')));
	const auto &result = sink.results.last();
	if (mismatch || rescan)
	{
		QCOMPARE(totals.failed, 1);
		QCOMPARE(totals.succeeded, 0);
		QVERIFY(!result.sourceRemoved);
		QCOMPARE(get(path), payload);
		QVERIFY(!QFileInfo::exists(deleting ? trashRoot : destination + QStringLiteral("/clip.omf")));
		if (rescan)
			QCOMPARE(result.message, refusal);
		else
		{
			QVERIFY(result.message.contains(QStringLiteral("rescan before proceeding"), Qt::CaseInsensitive));
			QVERIFY(result.message.contains(fromHeader ? QStringLiteral("Avid identity") : QStringLiteral("PMR/MDB FileMobId")));
		}
	}
	else
	{
		QVERIFY2(totals.succeeded == 1, qPrintable(result.message));
		QCOMPARE(totals.failed, 0);
		const auto landed = result.destination;
		if (deleting)
			QVERIFY(landed.startsWith(trashRoot + '/'));
		else
			QCOMPARE(landed, destination + QStringLiteral("/clip.omf"));
		QCOMPARE(get(landed), payload);
		QCOMPARE(result.sourceRemoved, request.kind != OperationKind::Copy);
		if (request.kind == OperationKind::Copy)
			QCOMPARE(get(path), payload);
		else
			QVERIFY(!QFileInfo::exists(path));
		if (deleting)
		{
			const auto records = OperationJournal::scan(journals);
			QCOMPARE(records.size(), qsizetype(1));
			const auto &forward = records.front();
			QCOMPARE(forward.entries.size(), qsizetype(1));
			QCOMPARE(forward.entries.front().trashProvider, QStringLiteral("mediamuster"));
			OperationRequest undo;
			undo.kind = OperationKind::Undo;
			undo.undoEnabled = true;
			undo.undoJournalPath = forward.path;
			const auto restored = runner.run(undo, journals);
			QVERIFY2(restored.succeeded == 1, qPrintable(sink.messages.join('\n')));
			QCOMPARE(get(path), payload);
			QVERIFY(!QFileInfo::exists(landed));
			QVERIFY(OperationRecovery::readSummary(journals).resumable.isEmpty());
		}
	}
}

void TestFileOperations::operation_scan_receipt_is_required_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<bool>("stamped");
	for (const auto kind : {OperationKind::Copy, OperationKind::Move, OperationKind::Delete, OperationKind::Rename})
		for (const bool stamped : {false, true})
		{
			const QString name = operationKindName(kind) +
								 (stamped ? QStringLiteral("-stamped") : QStringLiteral("-unstamped"));
			QTest::newRow(qPrintable(name)) << int(kind) << stamped;
		}
}

void TestFileOperations::operation_scan_receipt_is_required()
{
	QFETCH(int, kind);
	QFETCH(bool, stamped);
	Fixture f;
	MediaFileRecord file;
	file.mediaFilePath = f.src;
	file.fileName = QFileInfo(f.src).fileName();
	file.sizeBytes = f.bytes.size();
	if (stamped)
		file.scanStamp = {f.src, VolumeIdentity::capture(f.src).identifier(), QFileInfo(f.src).lastModified(), {}, {}};
	QVERIFY(!file.operationScanReceipt);
	auto request = f.request(OperationKind(kind));
	request.items = OperationManager::itemsFromMediaFiles({file}, {});
	QVERIFY(request.items.front().headerIdentity);
	const QString refusal = QStringLiteral("Scan safety record unavailable. Rescan before proceeding.");
	QCOMPARE(request.items.front().headerIdentity->unavailableReason, refusal);
	if (OperationKind(kind) == OperationKind::Rename)
	{
		request.items.front().renameDst = f.dest + '/' + file.fileName;
		request.items.front().groupKey = QStringLiteral("clip");
	}
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceNetworkTrash = true;
	const auto refused = runner.run(request, f.journals + QStringLiteral("/missing"));
	QCOMPARE(refused.failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + '/' + file.fileName));
	QVERIFY(QDir(f.dest).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
	QVERIFY(!QFileInfo::exists(request.diagnosticTrashRoot));
	QVERIFY(!sink.results.isEmpty());
	QCOMPARE(sink.results.last().message, refusal);

	// Explicitly captured unknown identity is usable; an absent capture is not.
	file.scanStamp = {f.src, VolumeIdentity::capture(f.src).identifier(), QFileInfo(f.src).lastModified(), {}, {}};
	file.operationScanReceipt = OperationScanReceipt{};
	QVERIFY(file.operationScanReceipt);
	QVERIFY(file.operationScanReceipt->headerIdentity.fileMobId.isEmpty());
	QVERIFY(file.operationScanReceipt->headerIdentity.masterMobIds.isEmpty());
	QVERIFY(file.operationScanReceipt->headerIdentity.unavailableReason.isEmpty());
	QVERIFY(file.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
	request.items = OperationManager::itemsFromMediaFiles({file}, {});
	QVERIFY(request.items.front().headerIdentity);
	QVERIFY(request.items.front().headerIdentity->unavailableReason.isEmpty());
	if (OperationKind(kind) == OperationKind::Rename)
	{
		request.items.front().renameDst = f.dest + '/' + file.fileName;
		request.items.front().groupKey = QStringLiteral("clip");
	}
	const auto completed = runner.run(request, f.journals + QStringLiteral("/unknown"));
	QVERIFY2(completed.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(completed.failed, 0);
}

void TestFileOperations::changed_scan_header_cannot_become_unknown()
{
	Fixture f;
	MediaFileRecord file;
	file.mediaFilePath = f.src;
	file.fileName = QFileInfo(f.src).fileName();
	file.sizeBytes = f.bytes.size();
	file.scanStamp = {f.src, VolumeIdentity::capture(f.src).identifier(), QFileInfo(f.src).lastModified(), {}, {}};
	file.operationScanReceipt = OperationScanReceipt{OperationHeaderIdentity{{}, {}, QStringLiteral("The media header changed during scanning. Rescan before proceeding.")}, {}};
	QVERIFY(file.operationScanReceipt);
	auto request = f.request(OperationKind::Move);
	request.items = OperationManager::itemsFromMediaFiles({file}, {});
	QVERIFY(request.items[0].headerIdentity);
	QVERIFY(!request.items[0].headerIdentity->unavailableReason.isEmpty());
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + '/' + file.fileName));
	QVERIFY(!sink.results.isEmpty());
	QCOMPARE(sink.results.last().message, file.operationScanReceipt->headerIdentity.unavailableReason);
}

void TestFileOperations::conflicting_scan_header_is_refused_without_history()
{
	Fixture f;
	MediaFileRecord file;
	file.mediaFilePath = f.src;
	file.fileName = QFileInfo(f.src).fileName();
	file.sizeBytes = f.bytes.size();
	file.scanStamp = {f.src, VolumeIdentity::capture(f.src).identifier(), QFileInfo(f.src).lastModified(), {}, {}};
	file.operationScanReceipt = OperationScanReceipt{OperationHeaderIdentity{{}, {}, QStringLiteral("The media header has conflicting file identities. Rescan before proceeding.")}, {}};
	QVERIFY(file.operationScanReceipt);
	auto request = f.request(OperationKind::Move);
	request.items = OperationManager::itemsFromMediaFiles({file}, {});
	QVERIFY(request.items.front().headerIdentity);
	const QString refusal = QStringLiteral("The media header has conflicting file identities. Rescan before proceeding.");
	QCOMPARE(request.items.front().headerIdentity->unavailableReason, refusal);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + '/' + file.fileName));
	QVERIFY(!sink.results.isEmpty());
	QCOMPARE(sink.results.last().message, refusal);
}

// MARK: - Volume resolution and concurrent jobs

void TestFileOperations::journal_volume_paths_survive_two_resolutions()
{
	Fixture f;
	const QString first = QFileInfo(f.src).absolutePath(), second = f.root + "/second",
				  third = f.root + "/third";
	auto request = f.request();
	request.destRoot = first + "/copy";
	QString error;
	{
		OperationJournal journal;
		QVERIFY(journal.create(request, f.journals, error));
	}
	auto record = OperationJournal::scan(f.journals)[0];
	auto original = VolumeIdentity::capture(first);
	original.rootPath = first;
	record.volumes = {original};
	QVERIFY(QDir().rename(first, second));
	auto mounted = original;
	mounted.rootPath = second;
	QVERIFY2(OperationJournal::resolve(record, error, {mounted}), qPrintable(error));
	{
		OperationJournal journal;
		QVERIFY(journal.resume(record, error));
	}
	auto read = OperationJournal::readOne(record.path);
	QVERIFY(read);
	QCOMPARE(read->entries[0].item.src, second + "/clip.bin");
	QCOMPARE(read->request.destRoot, second + "/copy");
	QVERIFY(QDir().rename(second, third));
	mounted.rootPath = third;
	QVERIFY2(OperationJournal::resolve(*read, error, {mounted}), qPrintable(error));
	{
		OperationJournal journal;
		QVERIFY(journal.resume(*read, error));
	}
	const auto final = OperationJournal::readOne(record.path);
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
		OperationJournal journal;
		QVERIFY(journal.create(f.request(), f.journals, error));
	}
	auto record = OperationJournal::scan(f.journals)[0];
	auto wrong = record.volumes[0];
	wrong.uuid = "another-volume";
	wrong.serial += 1;
	QVERIFY(!OperationJournal::resolve(record, error, {wrong}));
	QCOMPARE(get(f.src), f.bytes);
}
void TestFileOperations::second_runner_cannot_change_files()
{
	Fixture f;
	QString error;
	auto lock = OperationJournal::acquire(f.journals, error);
	QVERIFY(lock);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OperationKind::Move), f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::facade_refuses_second_job_without_cancelling_first()
{
	Fixture first, second;
	QVERIFY(first.dir.isValid());
	QVERIFY(second.dir.isValid());
	ScopedJournalDirectory journals(first.journals);
	OperationManager manager;
	QSignalSpy finished(&manager, &OperationManager::operationFinished);
	auto firstRequest = first.request();
	auto secondRequest = second.request();
	manager.execute(firstRequest);
	QVERIFY(manager.isRunning());
	manager.execute(secondRequest);
	QVERIFY(manager.isRunning());
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(!manager.isRunning());
	QCOMPARE(get(first.dest + "/clip.bin"), first.bytes);
	QVERIFY(!QFile::exists(second.dest + "/clip.bin"));
	QString error;
	auto lock = OperationJournal::acquire(first.journals, error);
	QVERIFY2(lock, qPrintable(error));
}
// MARK: - Rebalance dispatch and validation

void TestFileOperations::rebalance_cancel_before_queued_dispatch_keeps_source()
{
	Fixture f;
	ScopedJournalDirectory journals(f.journals);
	const QString root = f.root + "/Avid MediaFiles/MXF";
	const QString source = root + "/1/clip.mxf";
	const QByteArray bytes("Disposable media");
	put(source, bytes);
	RebalancePlan plan;
	plan.mxfRootPath = root;
	plan.ops.append({source, NumberedMxfFolder{{}, 2}, {}, bytes.size(), -1, {}, {}});
	QCOMPARE(RebalancePlanner::requestForPlan(plan).items.size(), 1);

	Rebalancer rebalancer;
	class CancelAtDispatch : public QObject
	{
	public:
		explicit CancelAtDispatch(Rebalancer &worker) : m_worker(worker) {}
		bool cancelled = false;

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override
		{
			// The first queued call is the prepared request's GUI handoff.
			// Cancel before delivery, after the preflight cancellation check.
			if (event->type() == QEvent::MetaCall && !cancelled)
			{
				cancelled = true;
				m_worker.cancel();
			}
			return QObject::eventFilter(watched, event);
		}

	private:
		Rebalancer &m_worker;
	} cancelAtDispatch(rebalancer);
	rebalancer.installEventFilter(&cancelAtDispatch);
	QSignalSpy finished(&rebalancer, &Rebalancer::finished);
	rebalancer.executeAsync(plan);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(cancelAtDispatch.cancelled);
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QVERIFY(finished.first().at(2).toBool());
	QCOMPARE(get(source), bytes);
	QVERIFY(!QFile::exists(root + "/2/clip.mxf"));
	QVERIFY(OperationJournal::scan(f.journals).isEmpty());
}

void TestFileOperations::rebalance_replacement_ignores_queued_preparation_data()
{
	QTest::addColumn<bool>("oldPlanUnavailable");
	QTest::newRow("old-start-is-ignored") << false;
	QTest::newRow("old-failure-is-ignored") << true;
}

void TestFileOperations::rebalance_replacement_ignores_queued_preparation()
{
	QFETCH(bool, oldPlanUnavailable);
	Fixture f;
	ScopedJournalDirectory journals(f.journals);
	const QString root = f.root + "/Avid MediaFiles/MXF";
	const QString source = root + "/1/clip.mxf";
	const QByteArray bytes("Disposable media");
	put(source, bytes);
	RebalancePlan replacement;
	replacement.mxfRootPath = root;
	replacement.ops.append({source, NumberedMxfFolder{{}, 3}, {}, bytes.size(), -1, {}, {}});
	RebalancePlan original = replacement;
	original.ops[0].dest = NumberedMxfFolder{{}, 2};
	if (oldPlanUnavailable)
		original.mxfRootPath = root + "-offline";

	Rebalancer rebalancer;
	class ReplaceAtDispatch : public QObject
	{
	public:
		ReplaceAtDispatch(Rebalancer &worker, const RebalancePlan &plan)
			: m_worker(worker), m_plan(plan) {}
		bool replaced = false;

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override
		{
			if (event->type() == QEvent::MetaCall && !replaced)
			{
				replaced = true;
				m_worker.executeAsync(m_plan);
			}
			return QObject::eventFilter(watched, event);
		}

	private:
		Rebalancer &m_worker;
		const RebalancePlan &m_plan;
	} replaceAtDispatch(rebalancer, replacement);
	rebalancer.installEventFilter(&replaceAtDispatch);
	QSignalSpy finished(&rebalancer, &Rebalancer::finished);
	QSignalSpy aborted(&rebalancer, &Rebalancer::aborted);
	QSignalSpy results(&rebalancer, &Rebalancer::operationResult);
	rebalancer.executeAsync(original);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(replaceAtDispatch.replaced);
	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QVERIFY(!finished.first().at(2).toBool());
	QVERIFY(aborted.isEmpty());
	QCOMPARE(results.size(), 1);
	QCOMPARE(get(root + "/3/clip.mxf"), bytes);
	QVERIFY(!QFile::exists(root + "/2/clip.mxf"));
	QVERIFY(!QFile::exists(source));
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().entries.first().dst, root + "/3/clip.mxf");
}

void TestFileOperations::rebalance_rejected_preparation_aborts_data()
{
	QTest::addColumn<bool>("rootVanishes");
	QTest::newRow("mxf-root-disappears") << true;
	QTest::newRow("source-folder-disappears") << false;
}

void TestFileOperations::rebalance_rejected_preparation_aborts()
{
	QFETCH(bool, rootVanishes);
	Fixture f;
	ScopedJournalDirectory journals(f.journals);
	const QString root = f.root + "/Avid MediaFiles/MXF";
	const QString source = root + "/1/clip.mxf";
	const QByteArray bytes("Disposable media");
	put(source, bytes);
	RebalancePlan plan;
	plan.mxfRootPath = root;
	plan.ops.append({source, NumberedMxfFolder{{}, 2}, {}, bytes.size(), -1, {}, {}});
	QCOMPARE(RebalancePlanner::requestForPlan(plan).items.size(), 1);
	const QString originalFolder = rootVanishes ? root : root + "/1";
	const QString aside = originalFolder + "-offline";
	QVERIFY(QDir().rename(originalFolder, aside));
	const QString retainedSource = aside + (rootVanishes ? "/1/clip.mxf" : "/clip.mxf");

	Rebalancer rebalancer;
	QSignalSpy aborted(&rebalancer, &Rebalancer::aborted);
	QSignalSpy finished(&rebalancer, &Rebalancer::finished);
	QSignalSpy results(&rebalancer, &Rebalancer::operationResult);
	QSignalSpy progress(&rebalancer, &Rebalancer::progress);
	rebalancer.executeAsync(plan);
	QTRY_VERIFY(!aborted.isEmpty() || !finished.isEmpty());
	QCOMPARE(aborted.size(), 1);
	QVERIFY(!aborted.first().first().toString().isEmpty());
	QVERIFY(finished.isEmpty());
	QVERIFY(results.isEmpty());
	QVERIFY(progress.isEmpty());
	QVERIFY(OperationJournal::scan(f.journals).isEmpty());
	QCOMPARE(get(retainedSource), bytes);
	QVERIFY(!QFileInfo::exists(root + "/2/clip.mxf"));
}

void TestFileOperations::bella_rebalance_rescan_refuses_whole_plan_data()
{
	QTest::addColumn<bool>("changed");
	QTest::newRow("changed-during-scan") << true;
	QTest::newRow("presence-unconfirmed") << false;
}

void TestFileOperations::bella_rebalance_rescan_refuses_whole_plan()
{
	QFETCH(bool, changed);
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto base = OperationJournal::canonicalPath(temporary.path());
	const auto root = base + "/Avid MediaFiles/MXF";
	const auto first = root + "/1/first.mxf";
	const auto stale = root + "/2/stale.mxf";
	const auto firstDatabase = root + "/1/msmMMOB.mdb";
	const auto staleDatabase = root + "/2/msmFMID.pmr";
	put(first, "First media");
	put(stale, "Stale media");
	put(firstDatabase, "First folder database");
	put(staleDatabase, "Second folder database");
	auto staleEntry = operationEntryFor(stale);
	staleEntry.operationSafety = changed ? Bella::RescanReason::FileChanged
										 : Bella::RescanReason::FilePresenceUnconfirmed;

	RebalancePlan plan;
	plan.mxfRootPath = root;
	for (const auto &entry : {operationEntryFor(first), staleEntry})
	{
		const auto row = mediaFileRecordFromScanEntry(entry);
		plan.ops.append({row.mediaFilePath, NumberedMxfFolder{{}, 3}, {}, row.sizeBytes, row.modified.toMSecsSinceEpoch(), {}, operationItemFromMediaFile(row)});
	}
	// The first file could move and its database could be retired if only the
	// later per-file gate protected the stale file.
	const auto request = RebalancePlanner::requestForPlan(plan);
	QCOMPARE(request.items.size(), 2);
	QCOMPARE(request.items.first().src, first);
	QVERIFY(request.items.last().headerIdentity);
	const auto reason = request.items.last().headerIdentity->unavailableReason;
	QVERIFY(!reason.isEmpty());

	const auto journalPath = base + "/journals";
	ScopedJournalDirectory journals(journalPath);
	Rebalancer rebalancer;
	QSignalSpy aborted(&rebalancer, &Rebalancer::aborted);
	QSignalSpy finished(&rebalancer, &Rebalancer::finished);
	QSignalSpy results(&rebalancer, &Rebalancer::operationResult);
	rebalancer.executeAsync(plan);
	QTRY_VERIFY(!aborted.isEmpty() || !finished.isEmpty());
	QCOMPARE(aborted.size(), 1);
	const auto message = aborted.first().first().toString();
	QVERIFY(message.contains(QDir::toNativeSeparators(stale)));
	QVERIFY(message.contains(reason));
	QVERIFY(finished.isEmpty());
	QVERIFY(results.isEmpty());
	QCOMPARE(get(first), QByteArray("First media"));
	QCOMPARE(get(stale), QByteArray("Stale media"));
	QCOMPARE(get(firstDatabase), QByteArray("First folder database"));
	QCOMPARE(get(staleDatabase), QByteArray("Second folder database"));
	QVERIFY(!QFileInfo::exists(root + "/3"));
	QVERIFY(!QFileInfo::exists(base + "/_MediaMuster_Trash"));
	QVERIFY(OperationJournal::scan(journalPath).isEmpty());
}

void TestFileOperations::bella_rebalance_live_checks_and_recovery_data()
{
	QTest::addColumn<BellaOperationCase>("scenario");
	QTest::addColumn<bool>("resume");
	QTest::newRow("header-id-matches-and-undo") << BellaOperationCase::HeaderMatch << false;
	QTest::newRow("header-full-id-differs") << BellaOperationCase::HeaderMismatch << false;
	QTest::newRow("database-id-matches-resume-and-undo") << BellaOperationCase::DatabaseMatch << true;
	QTest::newRow("database-full-id-differs") << BellaOperationCase::DatabaseMismatch << false;
	QTest::newRow("physical-only-and-undo") << BellaOperationCase::PhysicalOnly << false;
}

void TestFileOperations::bella_rebalance_live_checks_and_recovery()
{
	QFETCH(BellaOperationCase, scenario);
	QFETCH(bool, resume);
	const bool database = scenario == BellaOperationCase::DatabaseMatch ||
						  scenario == BellaOperationCase::DatabaseMismatch;
	const bool mismatch = scenario == BellaOperationCase::HeaderMismatch ||
						  scenario == BellaOperationCase::DatabaseMismatch;
	const bool checkId = scenario != BellaOperationCase::PhysicalOnly;
	const auto expectedId = QByteArray::fromHex("060a2b340101010501010f1013000000") +
							QByteArray(16, 'f');
	auto liveId = expectedId;
	if (mismatch)
		liveId[31] = 'g';
	const auto bytes = checkId ? mxfForOperationIdentity(liveId) : QByteArray("No usable file ID");

	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto base = OperationJournal::canonicalPath(temporary.path());
	const auto root = base + "/Avid MediaFiles/MXF";
	const auto source = root + "/1/track.mxf";
	const auto destination = root + "/2/track.mxf";
	const auto journals = base + "/journals";
	put(source, bytes);
	auto entry = operationEntryFor(source);
	if (checkId)
	{
		// This identity-only header deliberately has no technical descriptor.
		// Validate its authored ID through the borrowed-file safety entry point.
		QFile liveSource(source);
		QVERIFY(liveSource.open(QIODevice::ReadOnly));
		QVERIFY(Bella::mxfIdentityMatches(liveSource, MobId::format(liveId), {}));
		auto wrongId = liveId;
		wrongId[31] = char(quint8(wrongId[31]) ^ 1u);
		QVERIFY(!Bella::mxfIdentityMatches(liveSource, MobId::format(wrongId), {}));
		liveSource.close();
		const auto id = Bella::normalizeMobId(expectedId, Bella::MobIdLayout::Aaf32,
											  Bella::ByteOrder::BigEndian);
		QVERIFY(std::holds_alternative<Bella::MobId>(id));
		entry.operationSafety = Bella::VerifyFileId{std::get<Bella::MobId>(id),
													database ? Bella::FileIdOrigin::Database : Bella::FileIdOrigin::Header};
	}
	const auto row = mediaFileRecordFromScanEntry(entry);
	RebalancePlan plan;
	plan.mxfRootPath = root;
	plan.ops.append({source, NumberedMxfFolder{{}, 2}, {}, row.sizeBytes, row.modified.toMSecsSinceEpoch(), {}, operationItemFromMediaFile(row)});
	const auto request = RebalancePlanner::requestForPlan(plan);
	QCOMPARE(request.items.size(), 1);

	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	if (resume)
		runner.hooks.checkpoint = interruptAt("relocating");
	auto totals = runner.run(request, journals);
	const auto records = OperationJournal::scan(journals);
	QCOMPARE(records.size(), 1);
	const auto journal = records.first().path;
	QCOMPARE(records.first().entries.first().item.databaseFileMobIdToVerify,
			 database ? MobId::format(expectedId) : QString{});
	if (mismatch)
	{
		QCOMPARE(totals.failed, 1);
		QCOMPARE(totals.succeeded, 0);
		QCOMPARE(get(source), bytes);
		QVERIFY(!QFileInfo::exists(destination));
		QCOMPARE(sink.results.size(), 1);
		QVERIFY(sink.results.first().message.contains(
			database ? QStringLiteral("PMR/MDB FileMobId") : QStringLiteral("Avid identity")));
		return;
	}
	if (resume)
	{
		QCOMPARE(totals.needsAttention, 1);
		QCOMPARE(get(source), bytes);
		QVERIFY(!QFileInfo::exists(destination));
		const auto recovery = OperationRecovery::run(journals);
		QCOMPARE(recovery.resumable.size(), 1);
		QCOMPARE(recovery.resumable.first().total - recovery.resumable.first().finished, 1);
		const auto saved = OperationJournal::readOne(recovery.resumable.first().journalPath);
		QVERIFY(saved);
		QCOMPARE(saved->entries.first().item.databaseFileMobIdToVerify,
				 MobId::format(expectedId));
		runner.hooks.checkpoint = {};
		OperationRequest continuation;
		continuation.resumeJournalPath = journal;
		totals = runner.run(continuation, journals);
	}
	QVERIFY2(totals.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(destination), bytes);
	QVERIFY(!QFileInfo::exists(source));
	QVERIFY(OperationRecovery::readSummary(journals).resumable.isEmpty());

	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = journal;
	const auto undone = runner.run(undo, journals);
	QVERIFY2(undone.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(source), bytes);
	QVERIFY(!QFileInfo::exists(destination));
}

void TestFileOperations::rebalance_empty_plan_finishes_without_aborting()
{
	Fixture f;
	ScopedJournalDirectory journals(f.journals);
	RebalancePlan plan;
	plan.mxfRootPath = f.root + "/Avid MediaFiles/MXF";
	QVERIFY(QDir().mkpath(plan.mxfRootPath));
	Rebalancer rebalancer;
	QSignalSpy aborted(&rebalancer, &Rebalancer::aborted);
	QSignalSpy finished(&rebalancer, &Rebalancer::finished);
	rebalancer.executeAsync(plan);
	QTRY_VERIFY(!aborted.isEmpty() || !finished.isEmpty());
	QVERIFY(aborted.isEmpty());
	QCOMPARE(finished.size(), 1);
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QVERIFY(!finished.first().at(2).toBool());
}

void TestFileOperations::invalid_mxf_claims_are_refused_by_adapter()
{
	// The adapter must pass the scan claims into the shared identity gate.
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const QString base = OperationJournal::canonicalPath(temp.path());
	const QString root = base + "/Avid MediaFiles/MXF";
	const QString journalDirectory = base + "/journals";
	ScopedJournalDirectory journals(journalDirectory);
	QVector<MediaFileRecord> files;
	for (const auto &name : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")})
	{
		MediaFileRecord file;
		file.mediaFilePath = root + "/1/" + name + ".mxf";
		file.mediaFolderName = "1";
		file.masterMobId = MobId::format(QCryptographicHash::hash(
			("mob" + name.toUpper()).toUtf8(), QCryptographicHash::Sha256));
		file.sizeBytes = 1000;
		put(file.mediaFilePath, QByteArray(int(file.sizeBytes), '\0'));
		file.operationScanReceipt = OperationScanReceipt{};
		files.append(file);
	}
	QVERIFY(QDir().mkpath(root + "/2"));
	RebalancePlan plan = RebalancePlanner::computePlan(root, "Vol", files);
	plan.ops.clear();
	for (const MediaFileRecord &file : files)
		plan.ops.append(
			{file.mediaFilePath, NumberedMxfFolder{QString(), 2}, file.masterMobId, file.sizeBytes, -1, file.fileMobId, {}});

	auto rebalancer = std::make_unique<Rebalancer>();
	QSignalSpy finished(rebalancer.get(), &Rebalancer::finished);
	rebalancer->executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);
	rebalancer.reset();

	int landed = 0;
	for (const MediaFileRecord &file : files)
	{
		const bool atSource = QFile::exists(file.mediaFilePath);
		const bool atDest = QFile::exists(root + "/2/" + QFileInfo(file.mediaFilePath).fileName());
		QVERIFY(atSource != atDest);
		landed += atDest;
	}
	QCOMPARE(landed, 0); // Fake MXF IDs are correctly rejected by the engine.
	const auto records = OperationJournal::scan(journalDirectory);
	QCOMPARE(records.size(), 1);
	for (const auto &entry : records.first().entries)
		QVERIFY(entry.step != OperationJournal::Step::Done);
}

// MARK: - Durability and restart boundaries

void TestFileOperations::unsupported_directory_flush_preserves_originals()
{
	for (const auto kind : {OperationKind::Copy, OperationKind::Move})
	{
		Fixture f;
		put(f.dest + "/clip.bin", "existing media");
		Sink sink;
		std::atomic<bool> cancel{false};
		OperationRunner runner(sink, cancel);
		runner.hooks.directorySync = [](const QString &path, QString *error)
		{
			*error = "Directory flush unsupported: " + path;
			return FileSync::SyncResult::OkDegraded;
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
		QCOMPARE(totals.succeeded, kind == OperationKind::Copy ? 1 : 0);
		QCOMPARE(totals.retained, kind == OperationKind::Move ? 1 : 0);
		QCOMPARE(totals.failed + totals.needsAttention, 0);
		QVERIFY(chunks > 0); // Native APIs choose their own chunk sizes.
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(f.dest + "/clip.bin"), QByteArray("existing media"));
		QCOMPARE(get(f.dest + "/clip (2).bin"), QByteArray("late writer"));
		QCOMPARE(get(f.dest + "/clip (3).bin"), f.bytes);
		QVERIFY(!sink.results.last().sourceRemoved);
		QVERIFY(sink.results.last().message.contains("does not support"));
		const auto entry = OperationJournal::scan(f.journals)[0].entries[0];
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
		OperationRunner runner(sink, cancel);
		bool publishing = false;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{ publishing = publishing || stage == "publishing"; };
		runner.hooks.directorySync = [&](const QString &path, QString *error)
		{
			if (!afterPublication || (publishing && path.contains("/.mediamuster-")))
			{
				*error = "Injected directory I/O error";
				return FileSync::SyncResult::Failed;
			}
			if (publishing)
			{
				*error = "Directory flush unsupported";
				return FileSync::SyncResult::OkDegraded;
			}
			return FileSync::syncDirectory(path, error);
		};
		const auto totals = runner.run(f.request(), f.journals);
		QCOMPARE(totals.succeeded + totals.retained, 0);
		QCOMPARE(totals.failed + totals.needsAttention, 1);
		QCOMPARE(get(f.src), f.bytes);
		QVERIFY(sink.results.last().message.contains("Injected directory I/O error"));
		if (afterPublication)
		{
			QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
			const auto entry = OperationJournal::scan(f.journals)[0].entries[0];
			QCOMPARE(entry.step, OperationJournal::Step::NeedsAttention);
			QVERIFY(!entry.copyDurable);
		}
		else
			QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
}
void TestFileOperations::unsupported_relocation_keeps_media_and_databases()
{
	for (const auto kind : {OperationKind::Delete, OperationKind::Rename})
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
		OperationRunner runner(sink, cancel);
		bool attemptedRelocation = false;
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{ attemptedRelocation = attemptedRelocation || stage == "relocating"; };
		runner.hooks.directorySync = [](const QString &, QString *error)
		{
			*error = "Directory flush unsupported";
			return FileSync::SyncResult::OkDegraded;
		};
		const auto totals = runner.run(request, f.journals);
		QCOMPARE(totals.succeeded + totals.retained, 0);
		QCOMPARE(totals.failed + totals.needsAttention, 1);
		QVERIFY(!attemptedRelocation);
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(database), QByteArray("original Avid database"));
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
		QCOMPARE(OperationJournal::scan(f.journals)[0].entries.size(), 1);
	}
}
void TestFileOperations::directory_creation_propagates_durability()
{
	using Sync = FileSync::SyncResult;
	for (const auto childResult : {Sync::Ok, Sync::Failed})
	{
		Fixture f;
		QString error;
		const auto status = OperationFile::makeDirectory(f.root + "/parent/child", error,
														 [&](const QString &path, QString *detail)
														 {
															 *detail = path == f.root ? "parent unsupported" : "child I/O error";
															 return path == f.root ? Sync::OkDegraded : childResult;
														 });
		QCOMPARE(status, childResult == Sync::Failed ? Sync::Failed : Sync::OkDegraded);
		QVERIFY(error.contains(childResult == Sync::Failed ? "child I/O error" : "parent unsupported"));
	}
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
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 2);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.dest + "/clip (2).bin"), QByteArray("other"));
}
void TestFileOperations::regenerated_database_is_retired_on_resume()
{
	Fixture f;
	auto request = f.request(OperationKind::Rename);
	request.items[0].renameDst = f.dest + "/clip.bin";
	request.diagnosticTrashRoot = f.root + "/_MediaMuster_Trash";
	const auto database = f.root + "/source/msmMMOB.mdb";
	put(database, "original database");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = [](const QString &stage, const auto &e)
	{
		if (stage == "relocating" && !e.item.maintenance)
			throw std::runtime_error("interrupt before media move");
	};
	runner.run(request, f.journals);
	QVERIFY(!QFile::exists(database));
	QVERIFY(QFile::exists(f.src));
	const auto record = OperationJournal::scan(f.journals)[0];
	put(database, "regenerated database");
	OperationRequest resume;
	resume.resumeJournalPath = record.path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	const auto final = OperationJournal::scan(f.journals)[0];
	QCOMPARE(final.entries.size(), 3);
	QCOMPARE(get(final.entries[1].dst), QByteArray("original database"));
	QCOMPARE(get(final.entries[2].dst), QByteArray("regenerated database"));
	QVERIFY(!QFile::exists(database));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
}
void TestFileOperations::torn_tail_retains_valid_prefix()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = interruptAt("published");
	runner.run(f.request(), f.journals);
	const auto record = OperationJournal::scan(f.journals)[0];
	const auto prefix = get(record.path);
	{
		QFile file(record.path);
		QVERIFY(file.open(QIODevice::Append));
		file.write("{unfinished");
	}
	OperationRecovery::run(f.journals);
	QVERIFY(get(record.path).startsWith(prefix));
	QVERIFY(!OperationJournal::scan(f.journals)[0].torn);
	QCOMPARE(OperationJournal::scan(f.journals)[0].entries[0].step, OperationJournal::Step::Done);
}
void TestFileOperations::abrupt_process_exit_is_recoverable_data()
{
	QTest::addColumn<QString>("checkpoint");
	QTest::newRow("before-publication") << QStringLiteral("copy-ready");
	QTest::newRow("after-publication") << QStringLiteral("published");
}
void TestFileOperations::abrupt_process_exit_is_recoverable()
{
	QFETCH(QString, checkpoint);
	const bool hasPublished = checkpoint == QStringLiteral("published");
	Fixture f;
	const auto destination = f.dest + QStringLiteral("/clip.bin");
	QProcess child;
	child.start(QCoreApplication::applicationFilePath(),
				{"--crash-copy", checkpoint, f.src, f.dest, f.journals});
	QVERIFY2(child.waitForFinished(30000), qPrintable(child.errorString()));
	QCOMPARE(child.exitStatus(), QProcess::NormalExit);
	QCOMPARE(child.exitCode(), 81);
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const auto &record = records.first();
	QCOMPARE(record.entries.size(), 1);
	const auto &entry = record.entries.first();
	QCOMPARE(entry.step, hasPublished ? OperationJournal::Step::Published : OperationJournal::Step::CopyReady);
	QCOMPARE(get(f.src), f.bytes);
	if (hasPublished)
		QCOMPARE(get(destination), f.bytes);
	else
		QCOMPARE(QFileInfo(entry.temp).size(), qint64(f.bytes.size()));

	const auto recovery = OperationRecovery::run(f.journals);
	if (!hasPublished)
	{
		QCOMPARE(recovery.resumable.size(), 1);
		QCOMPARE(recovery.resumable.first().journalPath, record.path);
		QCOMPARE(recovery.resumable.first().finished, 0);
		QVERIFY(!QFile::exists(destination));
		return;
	}
	QVERIFY(recovery.resumable.isEmpty());
	QVERIFY(recovery.restorable.isEmpty());
	QVERIFY(recovery.undoCandidate);
	QCOMPARE(recovery.undoCandidate->path, record.path);
	QCOMPARE(recovery.undoCandidate->entries.size(), 1);
	QCOMPARE(recovery.undoCandidate->entries.first().step, OperationJournal::Step::Done);
	QVERIFY(recovery.undoCandidate->entries.first().cleanup.isEmpty());
	QCOMPARE(get(destination), f.bytes);
	const auto reconciled = OperationJournal::readOne(record.path);
	QVERIFY(reconciled);
	QCOMPARE(reconciled->entries.size(), 1);
	QCOMPARE(reconciled->entries.first().step, OperationJournal::Step::Done);
}
void TestFileOperations::rebalance_late_conflict_stops_remaining_members()
{
	Fixture f;
	auto request = f.request(OperationKind::Rename);
	request.items.clear();
	for (int n = 0; n < 3; ++n)
	{
		OperationItem item;
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
	OperationRunner runner(sink, cancel);
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
	QCOMPARE(sink.messages.last(), QStringLiteral("Rebalance incomplete: 1 file moved; 2 not completed."));
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
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	QCOMPARE(runner.run(f.request(OperationKind::Move), f.journals).failed, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(sink.results.last().message.contains("Encrypted"));
}
#endif
// MARK: - Copy before original removal

void TestFileOperations::move_copies_every_file_before_removal()
{
	Fixture f;
	auto request = f.request(OperationKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	const QByteArray secondBytes(4 * 1024 * 1024, 'a');
	second.bytes = secondBytes.size();
	put(second.src, secondBytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	int published = 0, retiring = 0, removed = 0;
	QSet<int> readyCopies;
	QStringList failures;
	auto checkContents = [&](const QString &path, const QByteArray &expected, const QString &stage)
	{
		// The engine still holds its protected source handle at publication.
		// An ordinary QFile reader on Windows does not share DELETE access;
		// OpFile's read-only fallback can observe that handle without weakening it.
		QString error;
		auto file = OperationFile::open(path, false, error);
		if (!file)
			failures.append(QString("%1: cannot inspect %2: %3").arg(stage, path, error));
		else
		{
			const auto actual = file->io().readAll();
			if (file->io().error() != QFileDevice::NoError)
				failures.append(QString("%1: cannot read %2: %3")
									.arg(stage, path, file->io().errorString()));
			else if (actual != expected)
				failures.append(QString("%1: contents changed at %2 (expected %3 bytes, read %4)")
									.arg(stage, path)
									.arg(expected.size())
									.arg(actual.size()));
		}
	};
	runner.hooks.checkpoint = [&](const QString &stage, const auto &entry)
	{
		if (stage == "published")
		{
			++published;
			if (entry.copyDurable && entry.metadataComplete)
				readyCopies.insert(entry.id);
			else
				failures.append(QString("Published copy %1 is not ready for original removal.")
									.arg(entry.id));
			checkContents(f.src, f.bytes, stage);
			checkContents(second.src, secondBytes, stage);
		}
		if (stage == "before-source-retirement")
		{
			++retiring;
			if (published != request.items.size() || readyCopies.size() != request.items.size())
				failures.append(QString("Original removal started with %1 published and %2 ready copies.")
									.arg(published)
									.arg(readyCopies.size()));
			checkContents(entry.item.src, entry.item.src == f.src ? f.bytes : secondBytes, stage);
		}
		if (stage == "source-unlinked")
			++removed;
	};
	const auto t = runner.run(request, f.journals);
	QVERIFY2(t.succeeded == 2, qPrintable(sink.messages.join('\n')));
	QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
	QCOMPARE(published, 2);
	QCOMPARE(readyCopies.size(), 2);
	QCOMPARE(retiring, 2);
	QCOMPARE(removed, 2);
	QVERIFY(!QFile::exists(f.src));
	QVERIFY(!QFile::exists(second.src));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), secondBytes);
	QCOMPARE(sink.results.size(), 2);
	QVERIFY(OperationJournal::scan(f.journals)[0].copiesComplete);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move complete: 2 files moved to %1.").arg(f.dest)});
}
void TestFileOperations::changed_completed_destination_retains_original_data()
{
	QTest::addColumn<bool>("resume");
	QTest::newRow("before-removal") << false;
	QTest::newRow("before-resumed-removal") << true;
}

void TestFileOperations::changed_completed_destination_retains_original()
{
	QFETCH(bool, resume);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	const QByteArray replacement(f.bytes.size(), 'r');
	QString destination;
	bool replaced = false;
	const auto replaceDestination = [&](const QString &path)
	{
		destination = path;
		// Preserve the completed copy separately. A new object now occupies its
		// published name, and must never justify removing the original.
		if (!QFile::rename(path, path + QStringLiteral(".external")))
			return;
		put(path, replacement);
		replaced = true;
	};
	if (resume)
		runner.hooks.checkpoint = interruptAt(QStringLiteral("copies-complete"));
	else
		runner.hooks.checkpoint = [&](const QString &stage, const auto &entry)
		{
			if (stage == QStringLiteral("copies-complete"))
				replaceDestination(entry.dst);
		};
	auto totals = runner.run(f.request(OperationKind::Move), f.journals);
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const auto journalPath = records.front().path;
	if (resume)
	{
		QVERIFY(totals.needsAttention > 0);
		QCOMPARE(get(f.src), f.bytes);
		QCOMPARE(get(records.front().entries.front().dst), f.bytes);
		replaceDestination(records.front().entries.front().dst);
		QVERIFY(replaced);
		const auto recovery = OperationRecovery::run(f.journals);
		QVERIFY(recovery.opsFlagged > 0);
		runner.hooks = {};
		OperationRequest continuation;
		continuation.resumeJournalPath = journalPath;
		totals = runner.run(continuation, f.journals);
	}
	QVERIFY(replaced);
	QCOMPARE(totals.succeeded, 0);
	QVERIFY(totals.needsAttention > 0 || totals.retained > 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(destination), replacement);
	QCOMPARE(get(destination + QStringLiteral(".external")), f.bytes);
	const auto saved = OperationJournal::readOne(journalPath);
	QVERIFY(saved);
	QVERIFY(!saved->entries.front().sourceRemoved);
	QVERIFY(!saved->entries.front().complete());
	if (!resume)
	{
		QVERIFY(!sink.results.isEmpty());
		QCOMPARE(sink.results.last().message,
				 QStringLiteral("The completed destination is missing or changed: ") + destination);
	}
}

void TestFileOperations::failed_copy_continues_and_blocks_all_original_removal()
{
	Fixture f;
	auto request = f.request(OperationKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	put(f.dest + "/clip.bin", "unexpected conflict");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	const auto t = runner.run(request, f.journals);
	QCOMPARE(t.failed, 1);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(get(second.src), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	QVERIFY(!OperationJournal::scan(f.journals)[0].copiesComplete);
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move incomplete: 0 files moved to %1; 2 not completed.").arg(f.dest)});
}
void TestFileOperations::explicit_skip_survives_disappearing_conflict()
{
	Fixture f;
	auto request = f.request(OperationKind::Move, "skip");
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	second.policy.clear();
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	const auto t = runner.run(request, f.journals);
	QCOMPARE(t.skipped, 1);
	QCOMPARE(t.succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(second.src));
	QCOMPARE(copyMoveSummaries(sink),
			 QStringList{QStringLiteral("Move complete: 1 file moved to %1; 1 skipped.").arg(f.dest)});
}
void TestFileOperations::removal_crash_boundaries_resume()
{
	for (const auto &point : {"copies-complete", "before-source-retirement", "source-retired", "source-unlinked"})
	{
		Fixture f;
		Sink sink;
		std::atomic<bool> cancel{false};
		OperationRunner runner(sink, cancel);
		runner.hooks.forceCopy = true;
		runner.hooks.checkpoint = interruptAt(point);
		auto request = f.request(OperationKind::Move);
		QVERIFY(runner.run(request, f.journals).needsAttention > 0);
		auto saved = OperationJournal::scan(f.journals).first();
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		OperationRequest resume;
		resume.resumeJournalPath = saved.path;
		runner.hooks = {};
		const auto t = runner.run(resume, f.journals);
		QVERIFY2(t.needsAttention == 0 && t.failed == 0,
				 qPrintable(QString(point) + ": " + sink.messages.join('\n')));
		QVERIFY(!QFile::exists(f.src));
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		QVERIFY(OperationJournal::readOne(saved.path)->entries[0].complete());
	}
}
// MARK: - Undo and local Trash

void TestFileOperations::undo_copy_is_gated_and_claims_forward()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(), f.journals).succeeded, 1);
	const auto forward = OperationJournal::scan(f.journals).first();
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoJournalPath = forward.path;
	QVERIFY(runner.run(undo, f.journals).needsAttention > 0);
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	undo.undoEnabled = true;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!OperationJournal::readOne(forward.path)->undoPath.isEmpty());
	QVERIFY(sink.results.last().restoredOriginalPath.isEmpty()); // Undo Copy restores no original.
	QCOMPARE(sink.messages.count(QStringLiteral("Undo complete: 1 change reversed.")), 1);
	QVERIFY(sink.results.last().message.isEmpty());
	QVERIFY(!OperationJournal::latestUndoable(f.journals));
	OperationRequest resume;
	resume.resumeJournalPath = forward.path;
	QVERIFY(runner.run(resume, f.journals).needsAttention > 0);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::undo_partial_move_restores_only_completed_changes()
{
	Fixture f;
	auto request = f.request(OperationKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "source-removed" && e.id == 0)
			cancel = true;
	};
	const auto first = runner.run(request, f.journals);
	QVERIFY(first.cancelled);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(second.src), f.bytes);
	const auto forward = OperationJournal::scan(f.journals).first();
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	cancel = false;
	runner.hooks = {};
	sink.results.clear();
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 2, qPrintable(sink.messages.join('\n')));
	QCOMPARE(sink.results.size(), 2);
	QCOMPARE(sink.results[0].restoredOriginalPath, f.src);
	QVERIFY(sink.results[1].restoredOriginalPath.isEmpty()); // Discarding a redundant copy.
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
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "done" && e.id == 0)
			cancel = true;
	};
	QVERIFY(runner.run(request, f.journals).cancelled);
	const auto forward = OperationJournal::scan(f.journals).first();
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
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
	undo.undoJournalPath = OperationJournal::scan(changed.journals).first().path;
	put(changed.dest + "/clip.bin", "a changed file");
	QVERIFY(runner.run(undo, changed.journals).needsAttention > 0);
	QCOMPARE(get(changed.dest + "/clip.bin"), QByteArray("a changed file"));
}
void TestFileOperations::undo_move_resumes_after_publication_data()
{
	QTest::addColumn<bool>("changedDestination");
	QTest::newRow("unchanged-destination") << false;
	QTest::newRow("changed-destination") << true;
}
void TestFileOperations::undo_move_resumes_after_publication()
{
	QFETCH(bool, changedDestination);
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	auto request = f.request(OperationKind::Move);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = OperationJournal::scan(f.journals).first().path;
	runner.hooks = {};
	runner.hooks.checkpoint = interruptAt("published");
	QVERIFY(runner.run(undo, f.journals).needsAttention > 0);
	const auto pending = OperationJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].request.kind, OperationKind::Undo);
	if (changedDestination)
		put(f.src, "a changed destination");
	OperationRequest resume;
	resume.resumeJournalPath = pending[0].path;
	runner.hooks = {};
	sink.results.clear();
	const auto t = runner.run(resume, f.journals);
	if (changedDestination)
	{
		QVERIFY(t.needsAttention > 0);
		for (const auto &outcome : sink.results)
			QVERIFY(outcome.restoredOriginalPath.isEmpty());
		QCOMPARE(get(f.src), QByteArray("a changed destination"));
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	}
	else
	{
		QVERIFY2(t.needsAttention == 0 && t.failed == 0, qPrintable(sink.messages.join('\n')));
		QVERIFY(!sink.results.isEmpty());
		QCOMPARE(sink.results.last().restoredOriginalPath, f.src);
		QCOMPARE(get(f.src), f.bytes);
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
}
void TestFileOperations::local_trash_and_undo_roundtrip()
{
	Fixture f;
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	struct RestoreDisposable
	{
		Fixture &fixture;
		std::atomic<bool> &cancel;
		~RestoreDisposable()
		{
			if (QFile::exists(fixture.src))
				return;
			cancel.store(false);
			for (const auto &record : OperationJournal::scan(fixture.journals))
				for (const auto &entry : record.entries)
					if (!entry.trashReceipt.isEmpty() && entry.landed.valid())
					{
						const auto restored = OperationTrash::restore(entry.trashReceipt, fixture.src,
																	  entry.landed, cancel, entry.dst);
						if (QFile::exists(fixture.src))
							return;
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
	OperationRunner runner(sink, cancel);
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	const auto first = runner.run(request, f.journals);
	QVERIFY2(first.succeeded == 1, qPrintable(details()));
	QVERIFY(!QFile::exists(f.src));
	const auto forward = OperationJournal::scan(f.journals).first();
	if (forward.entries[0].trashProvider == QStringLiteral("system"))
		QVERIFY(!forward.entries[0].trashReceipt.isEmpty());
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(details()));
	QCOMPARE(get(f.src), f.bytes);
}

// MARK: - Native copy retries

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
	QCOMPARE(OperationCopier::isRetryableNativeError(nativeError), retryable);
}
void TestFileOperations::native_copy_retry_policy_data()
{
	QTest::addColumn<int>("kind");
	QTest::addColumn<int>("nativeError");
	QTest::addColumn<int>("failureCount");
	QTest::addColumn<int>("expectedAttempts");
	QTest::addColumn<bool>("copiedAll");
	QTest::newRow("move-transient-recovers") << int(OperationKind::Move) << transientCopyError << 1 << 2 << true;
	QTest::newRow("move-permanent-continues") << int(OperationKind::Move) << permanentCopyError << 1 << 1 << false;
	QTest::newRow("move-exhausted-continues") << int(OperationKind::Move) << transientCopyError << 10 << 3 << false;
	QTest::newRow("copy-transient-recovers") << int(OperationKind::Copy) << transientCopyError << 1 << 2 << true;
	QTest::newRow("copy-exhausted-continues") << int(OperationKind::Copy) << transientCopyError << 10 << 3 << false;
}
void TestFileOperations::native_copy_retry_policy()
{
	QFETCH(int, kind);
	QFETCH(int, nativeError);
	QFETCH(int, failureCount);
	QFETCH(int, expectedAttempts);
	QFETCH(bool, copiedAll);
	const bool moving = OperationKind(kind) == OperationKind::Move;
	Fixture f;
	auto request = f.request(OperationKind(kind));
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	int firstCalls = 0, secondCalls = 0;
	QStringList stagingPaths;
	runner.hooks.nativeCopyError = [&](const OperationJournal::Entry &entry)
	{
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
	QCOMPARE(totals.succeeded, copiedAll ? 2 : (moving ? 0 : 1));
	QCOMPARE(totals.retained, moving && !copiedAll ? 1 : 0);
	QCOMPARE(totals.needsAttention, 0);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const auto &record = records[0];
	QCOMPARE(record.entries[0].attempts, expectedAttempts);
	QCOMPARE(record.entries[1].attempts, 1);
	QCOMPARE(record.copiesComplete, moving && copiedAll);
	QCOMPARE(sink.results.size(), 2);
	QSet<QString> uniqueStaging(stagingPaths.cbegin(), stagingPaths.cend());
	QCOMPARE(uniqueStaging.size(), expectedAttempts);
	for (const auto &path : stagingPaths)
		if (OperationFile::occupied(path))
			QVERIFY(record.entries[0].artifacts.contains(path));
	if (copiedAll)
	{
		QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
		if (moving)
		{
			QVERIFY(!QFile::exists(f.src));
			QVERIFY(!QFile::exists(second.src));
		}
	}
	else
	{
		QCOMPARE(record.entries[0].step, OperationJournal::Step::Failed);
		QVERIFY(!record.entries[0].error.isEmpty());
		QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	}
	if (!moving || !copiedAll)
	{
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
	OperationRunner runner(sink, cancel);
	int calls = 0;
	runner.hooks.nativeCopyError = [&](const auto &)
	{
		++calls;
		if (duringCopy)
			cancel = true;
		return transientCopyError;
	};
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "before-copy-retry")
			cancel = true;
	};
	const auto totals = runner.run(f.request(), f.journals);
	QVERIFY(totals.cancelled);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(totals.succeeded, 0);
	QCOMPARE(calls, 1);
	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records[0].entries[0].attempts, 1);
	QCOMPARE(records[0].entries[0].step, OperationJournal::Step::Cancelled);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::native_copy_retry_stops_on_journal_failure()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int calls = 0;
	bool failJournal = false;
	runner.hooks.nativeCopyError = [&](const auto &)
	{
		++calls;
		failJournal = true;
		return transientCopyError;
	};
	runner.hooks.fail = [&](const QString &stage)
	{ return stage == "journal" && failJournal; };
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
	OperationRunner runner(sink, cancel);
	int publications = 0;
	runner.hooks.fail = [&](const QString &stage)
	{ return stage == "publish" && ++publications == 1; };
	const auto totals = runner.run(f.request(), f.journals);
	QCOMPARE(totals.failed, 1);
	QCOMPARE(publications, 1);
	QCOMPARE(OperationJournal::scan(f.journals)[0].entries[0].attempts, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
// MARK: - Rebalance undo and retained originals

void TestFileOperations::undo_rebalance_retires_regenerated_indexes()
{
	Fixture f;
	put(f.root + "/source/msmMMOB.mdb", "original database");
	auto request = f.request(OperationKind::Rename);
	request.items[0].renameDst = f.dest + "/clip.bin";
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	QCOMPARE(sink.messages, QStringList{QStringLiteral("Rebalance complete: 1 file moved.")});
	const auto forward = OperationJournal::scan(f.journals)[0];
	put(f.root + "/source/msmMMOB.mdb", "regenerated source");
	put(f.dest + "/msmFMID.pmr", "regenerated destination");
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	const auto t = runner.run(undo, f.journals);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	QVERIFY(!QFile::exists(f.root + "/source/msmMMOB.mdb"));
	QVERIFY(!QFile::exists(f.dest + "/msmFMID.pmr"));
	const auto inverse = OperationJournal::readOne(OperationJournal::readOne(forward.path)->undoPath);
	QVERIFY(inverse);
	int retired = 0;
	for (const auto &entry : inverse->entries)
		if (entry.item.maintenance)
		{
			++retired;
			QVERIFY(entry.complete());
		}
	QCOMPARE(retired, 2);
	QCOMPARE(sink.messages.count(QStringLiteral("Undo complete: 1 change reversed.")), 1);
}
void TestFileOperations::undo_original_in_retirement()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.fail = [](const QString &point)
	{ return point == "restore-original"; };
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "source-retired")
			cancel = true;
	};
	QVERIFY(runner.run(f.request(OperationKind::Move), f.journals).cancelled);
	const auto forward = OperationJournal::scan(f.journals)[0];
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(forward.entries[0].retirement), f.bytes);
	QVERIFY(OperationJournal::latestUndoable(f.journals));
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
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

// MARK: - System Trash and fallback consent

void TestFileOperations::network_delete_always_uses_mediamuster_trash()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceNetworkTrash = true;
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		return OperationTrash::Result{};
	};
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	QCOMPARE(nativeCalls, 0);
	QVERIFY(sink.trashFallbackPrompts.isEmpty());
	const auto saved = OperationJournal::scan(f.journals)[0];
	QCOMPARE(saved.entries[0].trashProvider, QStringLiteral("mediamuster"));
	QVERIFY(saved.entries[0].trashReceipt.isEmpty());
	QVERIFY(saved.entries[0].dst.contains("/_MediaMuster_Trash/"));
	QCOMPARE(get(saved.entries[0].dst), f.bytes);
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(sink.messages, QStringList{QStringLiteral("Delete complete: 1 file moved to Trash.")});
	QVERIFY(sink.results.last().message.isEmpty());
}

// These hooks emulate native API outcomes using only fixture files. They exercise
// the coordinator's consent, identity and recovery decisions on both platforms.
void TestFileOperations::native_trash_refusals_share_one_consent_and_undo()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	for (const auto &name : {"second.bin", "third.bin"})
	{
		auto item = request.items[0];
		item.src = f.root + "/source/" + name;
		item.name = name;
		put(item.src, f.bytes);
		request.items.append(item);
	}
	const QStringList reasons{"Too large for the native bin", "Native bin unavailable",
							  "Unrecognised native error 0xDEADBEEF"};
	Sink sink;
	bool originalsIntactAtPrompt = true;
	sink.trashFallbackAnswer = [&](const auto &items)
	{
		for (const auto &item : items)
			originalsIntactAtPrompt &= get(item.source) == f.bytes && !QFile::exists(item.destination);
		return true;
	};
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = reasons[nativeCalls++];
		return refused;
	};
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(nativeCalls, 3);
	QVERIFY2(totals.succeeded == 3, qPrintable(sink.messages.join('\n')));
	QCOMPARE(totals.failed + totals.needsAttention, 0);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	const auto offered = sink.trashFallbackPrompts[0];
	QCOMPARE(offered.size(), 3);
	QVERIFY(originalsIntactAtPrompt);
	const auto forward = OperationJournal::scan(f.journals)[0];
	for (int i = 0; i < request.items.size(); ++i)
	{
		QCOMPARE(offered[i].source, request.items[i].src);
		QCOMPARE(offered[i].reason, reasons[i]);
		QVERIFY(!offered[i].destination.isEmpty());
		QVERIFY(forward.entries[i].dst.startsWith(offered[i].destination + '/'));
		QVERIFY(forward.entries[i].trashFallbackApproved);
		QCOMPARE(forward.entries[i].trashProvider, QStringLiteral("mediamuster"));
		QVERIFY(forward.entries[i].trashReceipt.isEmpty());
		QCOMPARE(get(forward.entries[i].dst), f.bytes);
		QVERIFY(!QFile::exists(request.items[i].src));
	}
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	QCOMPARE(runner.run(undo, f.journals).succeeded, 3);
	QCOMPARE(nativeCalls, 3);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	for (int i = 0; i < request.items.size(); ++i)
	{
		QCOMPARE(get(request.items[i].src), f.bytes);
		QVERIFY(!QFile::exists(forward.entries[i].dst));
	}
}

void TestFileOperations::native_trash_fallback_declined_keeps_originals()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	auto second = request.items[0];
	second.src = f.root + "/source/second.bin";
	second.name = "second.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink; // No answer is fail-closed, exactly like Cancel/closing the dialog.
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = "Native bin unavailable";
		return refused;
	};
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(nativeCalls, 2);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0].size(), 2);
	QVERIFY(totals.cancelled);
	QCOMPARE(totals.succeeded, 0);
	const auto saved = OperationJournal::scan(f.journals)[0];
	for (const auto &entry : saved.entries)
	{
		QVERIFY(!entry.trashFallbackApproved);
		QCOMPARE(get(entry.item.src), f.bytes);
		QVERIFY(!QFile::exists(entry.dst));
	}
}

void TestFileOperations::native_trash_fallback_rechecks_changed_original()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	Sink sink;
	sink.trashFallbackAnswer = [&](const auto &)
	{
		put(f.src, "A replacement written while the dialog was open");
		return true;
	};
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = "Native bin refused the file";
		return refused;
	};
	const auto totals = runner.run(request, f.journals);
	QCOMPARE(nativeCalls, 1);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	QCOMPARE(totals.succeeded, 0);
	QVERIFY(totals.failed + totals.needsAttention > 0);
	QCOMPARE(get(f.src), QByteArray("A replacement written while the dialog was open"));
	const auto entry = OperationJournal::scan(f.journals)[0].entries[0];
	QVERIFY(!QFile::exists(entry.dst));
	QVERIFY(!entry.sourceRemoved);
}

void TestFileOperations::native_trash_failure_keeps_changed_source_visible()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	const QByteArray changed("Edited while the native Trash operation was running");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.nativeTrash = [&](const auto &)
	{
		put(f.src, changed);
		OperationTrash::Result failed;
		failed.outcome = OperationTrash::Outcome::Failed;
		failed.error = "Native Trash refused the changed file";
		return failed;
	};

	const auto totals = runner.run(request, f.journals);
	QCOMPARE(totals.succeeded, 0);
	QCOMPARE(totals.needsAttention, 1);
	QVERIFY(sink.trashFallbackPrompts.isEmpty());
	QCOMPARE(get(f.src), changed);
	QCOMPARE(sink.results.size(), 1);
	QCOMPARE(sink.results.first().state, OperationResult::State::NeedsAttention);
	QVERIFY(!sink.results.first().sourceRemoved);

	const auto records = OperationJournal::scan(f.journals);
	QCOMPARE(records.size(), 1);
	const auto &entry = records.first().entries.first();
	QCOMPARE(entry.step, OperationJournal::Step::NeedsAttention);
	QVERIFY(!entry.complete());
	QVERIFY(!entry.sourceRemoved);
	QVERIFY(entry.source.sameObject(OperationFile::inspect(f.src)));
	QVERIFY(!entry.source.unchanged(OperationFile::inspect(f.src)));
}

void TestFileOperations::native_trash_ambiguous_result_never_offers_fallback_data()
{
	QTest::addColumn<bool>("originalMoved");
	QTest::newRow("error-after-native-move") << true;
	QTest::newRow("original-present-but-native-result-also-exists") << false;
}
void TestFileOperations::native_trash_ambiguous_result_never_offers_fallback()
{
	QFETCH(bool, originalMoved);
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	const auto landed = f.root + "/native-result.bin";
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &entry)
	{
		++nativeCalls;
		if (originalMoved)
		{
			if (!QFile::rename(entry.item.src, landed))
				throw std::runtime_error("Cannot move disposable native-result fixture");
		}
		else
			put(landed, f.bytes);
		OperationTrash::Result ambiguous;
		ambiguous.path = landed;
		ambiguous.landed = OperationFile::inspect(landed);
		ambiguous.error = "Native move returned incomplete recovery information";
		return ambiguous;
	};
	const auto totals = runner.run(request, f.journals);
	QVERIFY(sink.trashFallbackPrompts.isEmpty());
	QCOMPARE(totals.succeeded, 0);
	QVERIFY(totals.failed + totals.needsAttention > 0);
	QCOMPARE(get(landed), f.bytes);
	QCOMPARE(QFile::exists(f.src), !originalMoved);
	if (!originalMoved)
		QCOMPARE(get(f.src), f.bytes);
	const auto entry = OperationJournal::scan(f.journals)[0].entries[0];
	QVERIFY(!entry.trashFallbackApproved);
	QVERIFY(entry.dst == landed || entry.artifacts.contains(landed));
	OperationRequest resume;
	resume.resumeJournalPath = OperationJournal::scan(f.journals)[0].path;
	QVERIFY(runner.run(resume, f.journals).needsAttention > 0);
	QCOMPARE(nativeCalls, 1);
	QVERIFY(sink.trashFallbackPrompts.isEmpty());
	QCOMPARE(get(landed), f.bytes);
}

void TestFileOperations::native_trash_cancellation_never_offers_fallback()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.nativeTrash = [](const auto &)
	{
		OperationTrash::Result cancelled;
		cancelled.outcome = OperationTrash::Outcome::Cancelled;
		cancelled.error = "User cancelled the native operation";
		return cancelled;
	};
	const auto totals = runner.run(request, f.journals);
	QVERIFY(sink.trashFallbackPrompts.isEmpty());
	QCOMPARE(totals.succeeded, 0);
	QVERIFY(totals.cancelled);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!OperationJournal::scan(f.journals)[0].entries[0].trashFallbackApproved);
}

void TestFileOperations::native_trash_success_excluded_from_fallback_batch()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	auto refused = request.items[0];
	refused.src = f.root + "/source/refused.bin";
	refused.name = "refused.bin";
	put(refused.src, f.bytes);
	request.items.append(refused);
	const auto nativeDestination = f.root + "/native-result.bin";
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.nativeTrash = [&](const auto &entry)
	{
		OperationTrash::Result result;
		if (entry.item.src == refused.src)
		{
			result.outcome = OperationTrash::Outcome::Unavailable;
			result.error = "Native bin refused this file";
			return result;
		}
		if (!QFile::rename(entry.item.src, nativeDestination))
			throw std::runtime_error("Cannot move disposable native-success fixture");
		result.outcome = OperationTrash::Outcome::Succeeded;
		result.path = nativeDestination;
		result.landed = OperationFile::inspect(nativeDestination);
		result.receipt = OperationTrashPlatform::receipt("test", nativeDestination, result.landed);
		return result;
	};
	QCOMPARE(runner.run(request, f.journals).succeeded, 2);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0].size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0][0].source, refused.src);
	QCOMPARE(get(nativeDestination), f.bytes);
	const auto entries = OperationJournal::scan(f.journals)[0].entries;
	QCOMPARE(entries[0].trashProvider, QStringLiteral("system"));
	QVERIFY(!entries[0].trashFallbackApproved);
	QCOMPARE(entries[1].trashProvider, QStringLiteral("mediamuster"));
	QVERIFY(entries[1].trashFallbackApproved);
}

void TestFileOperations::native_trash_fallback_crash_resume_data()
{
	QTest::addColumn<QString>("checkpoint");
	QTest::addColumn<bool>("approved");
	QTest::newRow("crash-awaiting-consent") << QString("trash-fallback-pending") << false;
	QTest::newRow("crash-after-durable-consent") << QString("trash-fallback-approved") << true;
}
void TestFileOperations::native_trash_fallback_crash_resume()
{
	QFETCH(QString, checkpoint);
	QFETCH(bool, approved);
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = "Native bin refused this file";
		return refused;
	};
	runner.hooks.checkpoint = interruptAt(checkpoint);
	QVERIFY(runner.run(request, f.journals).needsAttention > 0);
	QCOMPARE(get(f.src), f.bytes);
	QCOMPARE(nativeCalls, 1);
	const auto saved = OperationJournal::scan(f.journals)[0];
	QCOMPARE(saved.entries[0].step, OperationJournal::Step::TrashFallback);
	QCOMPARE(saved.entries[0].trashFallbackApproved, approved);
	QVERIFY(!QFile::exists(saved.entries[0].dst));
	QCOMPARE(sink.trashFallbackPrompts.size(), approved ? 1 : 0);
	runner.hooks.checkpoint = {};
	OperationRequest resume;
	resume.resumeJournalPath = saved.path;
	const auto totals = runner.run(resume, f.journals);
	QVERIFY2(totals.succeeded == 1, qPrintable(sink.messages.join('\n')));
	QCOMPARE(nativeCalls, 1); // Saved refusal/consent survives without another native operation.
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	const auto completed = OperationJournal::readOne(saved.path);
	QVERIFY(completed);
	QVERIFY(completed->entries[0].complete());
	QVERIFY(completed->entries[0].trashFallbackApproved);
	QCOMPARE(get(completed->entries[0].dst), f.bytes);
	QVERIFY(!QFile::exists(f.src));
}

void TestFileOperations::native_trash_fallback_batch_consent_survives_partial_completion()
{
	Fixture f;
	auto request = f.request(OperationKind::Delete);
	request.diagnosticTrashRoot.clear();
	auto second = request.items[0];
	second.src = f.root + "/source/second.bin";
	second.name = "second.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = "Native bin refused this file";
		return refused;
	};
	runner.hooks.checkpoint = [](const QString &stage, const auto &entry)
	{
		if (stage == "done" && entry.id == 0 && entry.trashFallbackApproved)
			throw std::runtime_error("Interrupted after the first approved file was moved");
	};
	QVERIFY(runner.run(request, f.journals).needsAttention > 0);
	QCOMPARE(nativeCalls, 2);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0].size(), 2);
	const auto saved = OperationJournal::scan(f.journals)[0];
	QVERIFY(saved.entries[0].complete());
	QVERIFY(!QFile::exists(f.src));
	QCOMPARE(get(saved.entries[0].dst), f.bytes);
	QCOMPARE(get(second.src), f.bytes);
	QCOMPARE(saved.entries[1].step, OperationJournal::Step::TrashFallback);
	QVERIFY(saved.entries[1].trashFallbackApproved);
	runner.hooks.checkpoint = {};
	OperationRequest resume;
	resume.resumeJournalPath = saved.path;
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(nativeCalls, 2);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	const auto completed = OperationJournal::readOne(saved.path);
	QVERIFY(completed);
	for (const auto &entry : completed->entries)
	{
		QVERIFY(entry.complete());
		QVERIFY(entry.trashFallbackApproved);
		QCOMPARE(get(entry.dst), f.bytes);
		QVERIFY(!QFile::exists(entry.item.src));
	}
}

void TestFileOperations::undo_copy_native_refusal_uses_consented_fallback()
{
	Fixture f;
	auto request = f.request();
	request.diagnosticTrashRoot.clear();
	Sink sink;
	sink.trashFallbackAnswer = [](const auto &)
	{ return true; };
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(request, f.journals).succeeded, 1);
	const auto forward = OperationJournal::scan(f.journals)[0];
	int nativeCalls = 0;
	runner.hooks.nativeTrash = [&](const auto &)
	{
		++nativeCalls;
		OperationTrash::Result refused;
		refused.outcome = OperationTrash::Outcome::Unavailable;
		refused.error = "Native bin is unavailable during Undo";
		return refused;
	};
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = forward.path;
	QCOMPARE(runner.run(undo, f.journals).succeeded, 1);
	QCOMPARE(nativeCalls, 1);
	QCOMPARE(sink.trashFallbackPrompts.size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0].size(), 1);
	QCOMPARE(sink.trashFallbackPrompts[0][0].source, f.dest + "/clip.bin");
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
	const auto inverse = OperationJournal::readOne(OperationJournal::readOne(forward.path)->undoPath);
	QVERIFY(inverse);
	QCOMPARE(inverse->entries[0].undoAction, QStringLiteral("discardCopy"));
	QVERIFY(inverse->entries[0].trashFallbackApproved);
	QCOMPARE(inverse->entries[0].trashProvider, QStringLiteral("mediamuster"));
	QCOMPARE(get(inverse->entries[0].dst), f.bytes);
}

// MARK: - Resume failures and late conflicts

void TestFileOperations::resume_flush_failure_stays_unfinished()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.checkpoint = interruptAt("published");
	QVERIFY(runner.run(f.request(), f.journals).needsAttention > 0);
	const auto saved = OperationJournal::scan(f.journals)[0];
	OperationRequest resume;
	resume.resumeJournalPath = saved.path;
	runner.hooks = {};
	runner.hooks.directorySync = [](const QString &, QString *error)
	{
		*error = "Injected directory I/O failure";
		return FileSync::SyncResult::Failed;
	};
	QVERIFY(runner.run(resume, f.journals).needsAttention > 0);
	QVERIFY(!OperationJournal::readOne(saved.path)->entries[0].complete());
	QCOMPARE(get(f.src), f.bytes);
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).needsAttention, 0);
	QVERIFY(OperationJournal::readOne(saved.path)->entries[0].complete());
}
void TestFileOperations::undo_late_collision_remains_resumable()
{
	Fixture f;
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	QCOMPARE(runner.run(f.request(OperationKind::Move), f.journals).succeeded, 1);
	OperationRequest undo;
	undo.kind = OperationKind::Undo;
	undo.undoEnabled = true;
	undo.undoJournalPath = OperationJournal::scan(f.journals)[0].path;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &e)
	{
		if (stage == "relocating")
			put(e.dst, "late conflict");
	};
	const auto failed = runner.run(undo, f.journals);
	QVERIFY(failed.failed + failed.needsAttention > 0);
	QCOMPARE(get(f.src), QByteArray("late conflict"));
	QCOMPARE(get(f.dest + "/clip.bin"), f.bytes);
	const auto pending = OperationJournal::interrupted(f.journals);
	QCOMPARE(pending.size(), 1);
	QVERIFY(QFile::remove(f.src));
	OperationRequest resume;
	resume.resumeJournalPath = pending[0].path;
	runner.hooks = {};
	QCOMPARE(runner.run(resume, f.journals).succeeded, 1);
	QCOMPARE(get(f.src), f.bytes);
	QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
}
void TestFileOperations::resume_continues_past_failed_source()
{
	Fixture f;
	auto request = f.request(OperationKind::Move);
	auto second = request.items[0];
	second.src = f.root + "/source/audio.bin";
	second.name = "audio.bin";
	put(second.src, f.bytes);
	request.items.append(second);
	put(f.dest + "/clip.bin", "unexpected conflict");
	Sink sink;
	std::atomic<bool> cancel{false};
	OperationRunner runner(sink, cancel);
	runner.hooks.forceCopy = true;
	runner.hooks.checkpoint = [&](const QString &stage, const auto &)
	{
		if (stage == "failed")
			cancel = true;
	};
	QVERIFY(runner.run(request, f.journals).cancelled);
	const auto saved = OperationJournal::scan(f.journals)[0];
	QVERIFY(QFile::remove(f.src)); // A failed source disappears while the app is stopped.
	OperationRequest resume;
	resume.resumeJournalPath = saved.path;
	cancel = false;
	runner.hooks = {};
	const auto t = runner.run(resume, f.journals);
	QCOMPARE(t.failed, 1);
	QCOMPARE(t.retained, 1);
	QCOMPARE(get(second.src), f.bytes);
	QCOMPARE(get(f.dest + "/audio.bin"), f.bytes);
	QVERIFY(!OperationJournal::readOne(saved.path)->copiesComplete);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	if (args.size() == 6 && args[1] == "--crash-copy")
	{
		Sink sink;
		std::atomic<bool> cancel{false};
		OperationRunner runner(sink, cancel);
		runner.hooks.checkpoint = [&](const QString &stage, const auto &)
		{
			if (stage == args[2])
				std::_Exit(81);
		};
		OperationRequest request;
		request.destRoot = args[4];
		OperationItem item;
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
#include "tst_file_operations.moc"
