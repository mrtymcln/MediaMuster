#include "oprunner.h"
#include "oprescue.h"
#include "opdiagnostics.h"
#include "mxfparser.h"
#include <QJsonArray>
#include <QTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFileInfo>
#include <QProcess>
#include <QCryptographicHash>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <stdexcept>
#ifdef Q_OS_MAC
#include <sys/mount.h>
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
} // namespace
class TestFileOperations : public QObject
{
	Q_OBJECT
  private slots:
	void copy_verifies_and_publishes();
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
	void journal_volume_paths_survive_two_resolutions();
	void mismatched_volume_is_never_session_matched();
	void second_runner_cannot_change_files();
	void debug_harness_uses_disposable_files();
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
	auto a = OpRescue::run(f.journals);
	auto b = OpRescue::run(f.journals);
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
	auto a = OpRescue::run(f.journals);
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
	auto a = OpRescue::run(f.journals);
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
	runner.hooks.fail = [](const QString &s) { return s == "source-remove"; };
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
	auto a = OpRescue::run(f.journals);
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
	auto a = OpRescue::run(f.journals);
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
			QCOMPARE(check["status"].toString(), QString("unsupported"));
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
		QCOMPARE(header.headerStatus, MxfMetadata::HeaderStatus::Complete);
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
	OpRescue::run(f.journals);
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
			QCOMPARE(OpRescue::run(f.journals).resumable.size(), 1);
			QVERIFY(!QFile::exists(f.dest + "/clip.bin"));
		}
		else
		{
			QVERIFY(OpRescue::run(f.journals).resumable.isEmpty());
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
