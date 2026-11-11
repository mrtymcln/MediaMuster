#include "scanner/bella_scanner.h"
#include "scanner/bella_scanner_p.h"
#include "scanner/discovery_p.h"
#include "scanner/read_jobs_p.h"
#include "test_bento.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

Q_DECLARE_METATYPE(Bella::Detail::PhysicalCheck)

// MARK: - Test helpers

namespace
{
	using Bella::Detail::PhysicalCheck;
	using Bella::Detail::PhysicalPresence;
	using Bella::Detail::ReadJobs;
	using Bella::Detail::ReadKind;
	using Bella::Detail::ReadReply;
	using Bella::Detail::ReadReport;
	constexpr int waitMs = 10000;

	enum class IdentityCase
	{
		Database,
		Header,
		HeaderWithoutId,
		CancelBeforeHeader,
		CancelBeforeRecheck,
		CancelWithZeroId
	};

	bool writeFile(const QString &path, const QByteArray &bytes = "test")
	{
		if (!QDir().mkpath(QFileInfo(path).absolutePath()))
			return false;
		QFile file(path);
		return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
	}

	QString pathIn(const QTemporaryDir &directory, const QString &relative)
	{
		const auto base = QFileInfo(directory.path()).canonicalFilePath();
		return base.isEmpty() ? QString{} : QDir(base).filePath(relative);
	}

	QByteArray pcmWave(int frames)
	{
		// Mono, 48 kHz, 16-bit PCM. Only the two sizes depend on the frame count.
		auto bytes = QByteArray::fromHex("524946460000000057415645666d7420100000000100010080bb000000770100020010006461746100000000");
		bytes.append(QByteArray(frames * 2, '\0'));
		qToLittleEndian(quint32(bytes.size() - 8), bytes.data() + 4);
		qToLittleEndian(quint32(frames * 2), bytes.data() + 40);
		return bytes;
	}

	QByteArray operationFileBento(bool mediaHeader, qint32 length = 250)
	{
		using TestMediaEngineBento::number;
		TestMediaEngineBento::TypedBento writer;
		const auto uid = QByteArray::fromHex("2a0000000b00000084030000");
		writer.head();
		writer.referenceArray(1, "OMFI:ObjectSpine", mediaHeader ? QVector<quint32>{100, 300} : QVector<quint32>{100});
		writer.add(100, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
		writer.add(100, "OMFI:MOBJ:MobID", "omfi:UID", uid);
		writer.add(100, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(200));
		writer.add(200, "OMFI:ObjID", "omfi:ObjectTag", "CDCI", true);
		writer.add(200, "OMFI:MDFL:SampleRate", "omfi:ExactEditRate", number<qint32>(25) + number<qint32>(1));
		writer.add(200, "OMFI:MDFL:Length", "omfi:Long", number(length), true);
		if (mediaHeader)
		{
			writer.add(300, "OMFI:ObjID", "omfi:ObjectTag", "TIFF", true);
			writer.add(300, "OMFI:TIFF:MobID", "omfi:UID", uid);
		}
		return writer.build();
	}

	const Bella::ScanEntry *entryAt(const Bella::ScanResult &result, const QString &path)
	{
		const auto found = std::find_if(result.entries.cbegin(), result.entries.cend(), [&](const auto &entry)
										{ return entry.path == path; });
		return found == result.entries.cend() ? nullptr : &*found;
	}

	Bella::ScanRequest treeRequest(const QString &path, bool includeOmf = false)
	{
		return {{{Bella::ScanLocation::Kind::MediaTree, path}}, includeOmf, false};
	}

	QString replyPath(int index)
	{
		return QStringLiteral("source-%1").arg(index);
	}

	ReadReport replyFor(ReadKind kind, int index)
	{
		switch (kind)
		{
		case ReadKind::Pmr:
		{
			Bella::PmrReport report;
			report.source.path = replyPath(index);
			return report;
		}
		case ReadKind::Mdb:
		{
			Bella::MdbReport report;
			report.path = replyPath(index);
			return report;
		}
		case ReadKind::Mxf:
		{
			Bella::MxfReport report;
			report.path = replyPath(index);
			return report;
		}
		case ReadKind::Omf:
		{
			Bella::OmfReport report;
			report.path = replyPath(index);
			return report;
		}
		}
		return std::exception_ptr{};
	}

	QString pathFrom(const ReadReport &reply)
	{
		return std::visit([](const auto &report) -> QString
						  {
			using Report = std::decay_t<decltype(report)>;
			if constexpr (std::is_same_v<Report, Bella::PmrReport>)
				return report.source.path;
			else if constexpr (std::is_same_v<Report, std::exception_ptr>)
				return {};
			else
				return report.path; }, reply);
	}

	// The queued callable is the sole owner. ReadJobs publishes its reply
	// before the worker destroys that callable.
	struct CallableFinished
	{
		explicit CallableFinished(QSemaphore &done) : done(done) {}
		~CallableFinished() { done.release(); }
		QSemaphore &done;
	};

	struct PausedWorkerCleanup
	{
		PausedWorkerCleanup(QSemaphore &started, QSemaphore &resume, std::atomic_bool &finished)
			: started(started), resume(resume), finished(finished) {}
		~PausedWorkerCleanup()
		{
			started.release();
			resume.acquire();
			finished.store(true);
		}
		QSemaphore &started;
		QSemaphore &resume;
		std::atomic_bool &finished;
	};
}

Q_DECLARE_METATYPE(IdentityCase)

// MARK: - Test fixture

class TestBellaScanner final : public QObject
{
	Q_OBJECT

private slots:
	void managedDiscovery_data();
	void managedDiscovery();
	void interruptedFolderIsNotComplete();
	void boundedJobsKeepOutOfOrderReplies();
	void cancelDrainsAndWaitsForWorkerCleanup();
	void physicalRecheck_data();
	void physicalRecheck();
	void completedStageProgress_data();
	void completedStageProgress();
	void matchesAfterHeader_data();
	void matchesAfterHeader();
	void operationFileIdOrigin_data();
	void operationFileIdOrigin();
	void unconfirmedPhysicalFileIsKept();
	void cancelKeepsKnownUnreadDatabase();
	void discoveryCallbackFailureIsIncomplete();
	void organiserCallbackFailureStopsWithOutstandingWork();
};

// MARK: - Managed media discovery

void TestBellaScanner::managedDiscovery_data()
{
	QTest::addColumn<bool>("includeOmf");
	QTest::newRow("MXF only") << false;
	QTest::newRow("MXF and OMF") << true;
}

void TestBellaScanner::managedDiscovery()
{
	QFETCH(bool, includeOmf);
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto base = QFileInfo(temporary.path()).canonicalFilePath();
	QVERIFY(!base.isEmpty());
	const auto avid = base + QStringLiteral("/Avid MediaFiles");
	const auto mxf = avid + QStringLiteral("/MXF");
	const auto omf = base + QStringLiteral("/OMFI MediaFiles");
	const auto normal = mxf + QStringLiteral("/1");
	const auto interview = mxf + QStringLiteral("/Interview");
	const auto quarantined = mxf + QStringLiteral("/Quarantined Files");
	const auto empty = mxf + QStringLiteral("/Empty");
	const auto workstation = omf + QStringLiteral("/Workstation");
	const auto original = normal + QStringLiteral("/Take.MXF");
	const auto copy = interview + QStringLiteral("/Take.MXF");
	const auto isolated = quarantined + QStringLiteral("/Isolated.mxf");
	const auto rootAudio = omf + QStringLiteral("/Tone.WAV");
	const auto legacy = workstation + QStringLiteral("/Legacy.omf");
	const auto pmrPath = normal + QStringLiteral("/amaFMID.PMR");
	const auto mdbPath = normal + QStringLiteral("/msmMMOB.mdb");
	for (const auto &path : {original, copy, isolated, rootAudio, legacy, pmrPath, mdbPath,
							 mxf + QStringLiteral("/AtRoot.mxf"), normal + QStringLiteral("/.Hidden.mxf"),
							 normal + QStringLiteral("/WrongFamily.wav"), interview + QStringLiteral("/Nested/TooDeep.mxf"),
							 mxf + QStringLiteral("/Creating/Incomplete.mxf"), omf + QStringLiteral("/WrongFamily.mxf")})
		QVERIFY2(writeFile(path), qPrintable(path));
	QVERIFY(QDir().mkpath(empty));
	QVERIFY(QDir().mkpath(normal + QStringLiteral("/Directory.mxf")));
#ifndef Q_OS_WIN
	QVERIFY(QFile::link(original, normal + QStringLiteral("/Link.mxf")));
#endif

	const Bella::ScanRequest request{{{Bella::ScanLocation::Kind::MediaBase, base},
									  {Bella::ScanLocation::Kind::MediaTree, avid},
									  {Bella::ScanLocation::Kind::MediaTree, avid}},
									 includeOmf,
									 false};
	Bella::ScanControl control;
	Bella::ScanResult result;
	Bella::Detail::DiscoveredDatabases databases;
	Bella::Detail::discover(request, control, result, databases, {});
	QVERIFY(result.discoveryComplete);
	QVERIFY(result.problems.isEmpty());
	QCOMPARE(result.entries.size(), includeOmf ? qsizetype(5) : qsizetype(3));
	QVERIFY(entryAt(result, original));
	QVERIFY(entryAt(result, copy));
	QVERIFY(entryAt(result, isolated));
	QVERIFY(!entryAt(result, original)->quarantined);
	QVERIFY(entryAt(result, isolated)->quarantined);
	QVERIFY(!entryAt(result, original)->omfEra);
	QSet<quint64> ids;
	for (const auto &entry : result.entries)
	{
		QVERIFY(entry.kelpieId != 0);
		ids.insert(entry.kelpieId);
		QCOMPARE(entry.sizeBytes, std::optional<qint64>(4));
		QVERIFY(entry.modified.isValid());
	}
	QCOMPARE(ids.size(), result.entries.size());
	QSet<QString> expectedFolders{normal, interview, quarantined, empty};
	if (includeOmf)
	{
		QVERIFY(entryAt(result, rootAudio));
		QVERIFY(entryAt(result, legacy));
		QVERIFY(entryAt(result, rootAudio)->omfEra);
		QVERIFY(entryAt(result, legacy)->omfEra);
		expectedFolders.insert(omf);
		expectedFolders.insert(workstation);
	}
	QCOMPARE(databases.completedFolders, expectedFolders);
	QCOMPARE(databases.pmrs.size(), qsizetype(1));
	QCOMPARE(databases.mdbs.size(), qsizetype(1));
	QCOMPARE(databases.pmrs[0].source.path, pmrPath);
	QCOMPARE(databases.pmrs[0].reading, Bella::PmrReadResult::NotRead);
	QVERIFY(!databases.pmrs[0].records.knownSet);
	QVERIFY(databases.pmrs[0].diagnostics.isEmpty());
	QCOMPARE(databases.mdbs[0].path, mdbPath);
	QCOMPARE(databases.mdbs[0].reading, Bella::MdbReadResult::NotRead);
	QVERIFY(!databases.mdbs[0].catalogue.fileReferencesComplete);
	QVERIFY(databases.mdbs[0].problemNotes.isEmpty());
}

void TestBellaScanner::interruptedFolderIsNotComplete()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto tree = pathIn(temporary, QStringLiteral("Avid MediaFiles"));
	QVERIFY(!tree.isEmpty());
	const auto root = tree + QStringLiteral("/MXF");
	const auto first = root + QStringLiteral("/1");
	const auto next = root + QStringLiteral("/2");
	const auto retained = first + QStringLiteral("/Retained.mxf");
	QVERIFY(writeFile(retained));
	QVERIFY(writeFile(next + QStringLiteral("/NotVisited.mxf")));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	callbacks.discovering = [&](const QString &path)
	{
		if (path == next)
			control.cancel();
	};
	Bella::ScanResult result;
	Bella::Detail::DiscoveredDatabases databases;
	Bella::Detail::discover(treeRequest(tree), control, result, databases, callbacks);
	QVERIFY(control.cancelled());
	QVERIFY(!result.discoveryComplete);
	QCOMPARE(result.entries.size(), qsizetype(1));
	QCOMPARE(result.entries[0].path, retained);
	QCOMPARE(databases.completedFolders, QSet<QString>{first});
}

// MARK: - Worker coordination and cancellation

void TestBellaScanner::boundedJobsKeepOutOfOrderReplies()
{
	const std::array kinds{ReadKind::Pmr, ReadKind::Mdb, ReadKind::Mxf, ReadKind::Omf, ReadKind::Mdb};
	std::array<QSemaphore, 5> resume;
	std::array<QSemaphore, 5> finished;
	QSemaphore started;
	Bella::ScanControl control;
	ReadJobs jobs(control);
	const auto releaseOnFailure = qScopeGuard([&]
											  {
		for (auto &gate : resume)
			gate.release(); });
	const auto submit = [&](int index)
	{
		const auto slot = std::size_t(index);
		return jobs.submit({kinds[slot], index}, [&, index, slot, life = std::make_shared<CallableFinished>(finished[slot])]
						   {
			Q_UNUSED(life);
			started.release();
			resume[slot].acquire();
			return replyFor(kinds[slot], index); });
	};
	for (int i = 0; i < 4; ++i)
		QVERIFY(submit(i));
	QVERIFY(started.tryAcquire(4, waitMs));
	for (const int i : {3, 1, 0, 2})
	{
		resume[std::size_t(i)].release();
		QVERIFY(finished[std::size_t(i)].tryAcquire(1, waitMs));
	}

	// All four jobs have posted replies. The fifth slot opens only on collection.
	QCOMPARE(jobs.outstanding(), 4);
	QVERIFY(!jobs.submit({ReadKind::Mdb, 4}, []
						 { return replyFor(ReadKind::Mdb, 4); }));
	const auto first = jobs.take();
	QCOMPARE(first.tag.kind, ReadKind::Omf);
	QCOMPARE(first.tag.index, qsizetype(3));
	QCOMPARE(pathFrom(first.report), replyPath(3));
	QCOMPARE(jobs.outstanding(), 3);
	QVERIFY(submit(4));
	resume[4].release();
	QVERIFY(finished[4].tryAcquire(1, waitMs));
	for (const int i : {1, 0, 2, 4})
	{
		const auto reply = jobs.take();
		QCOMPARE(reply.tag.kind, kinds[std::size_t(i)]);
		QCOMPARE(reply.tag.index, qsizetype(i));
		QCOMPARE(pathFrom(reply.report), replyPath(i));
	}
	QCOMPARE(jobs.outstanding(), 0);
}

void TestBellaScanner::cancelDrainsAndWaitsForWorkerCleanup()
{
	QSemaphore reading, resumeReader, cleaningUp, resumeCleanup, collected, returned;
	Bella::ScanControl control;
	std::atomic_bool cleanupFinished{false};
	bool rejectedAfterCancel = false;
	bool organiserSawCleanup = false;
	std::optional<ReadReply> reply;
	std::thread organiser([&]
						  {
		{
			ReadJobs jobs(control);
			const bool accepted = jobs.submit({ReadKind::Mdb, 0},
				[&, life = std::make_shared<PausedWorkerCleanup>(cleaningUp, resumeCleanup, cleanupFinished)]() -> ReadReport
				{
					Q_UNUSED(life);
					reading.release();
					resumeReader.acquire();
					Bella::MdbReport report;
					report.reading = control.cancelled() ? Bella::MdbReadResult::Cancelled : Bella::MdbReadResult::Completed;
					return report;
				});
			if (accepted)
			{
				reply = jobs.take();
				rejectedAfterCancel = !jobs.submit({ReadKind::Pmr, 1}, [] { return replyFor(ReadKind::Pmr, 1); });
				collected.release();
			}
		}
		organiserSawCleanup = cleanupFinished.load();
		returned.release(); });
	const auto joinOnFailure = qScopeGuard([&]
										   {
		control.cancel();
		resumeReader.release();
		resumeCleanup.release();
		if (organiser.joinable())
			organiser.join(); });
	QVERIFY(reading.tryAcquire(1, waitMs));
	control.cancel();
	QVERIFY(control.stopRequested().load());
	resumeReader.release();
	QVERIFY(collected.tryAcquire(1, waitMs));
	QVERIFY(cleaningUp.tryAcquire(1, waitMs));
	QVERIFY(!returned.tryAcquire());
	resumeCleanup.release();
	QVERIFY(returned.tryAcquire(1, waitMs));
	organiser.join();
	QVERIFY(organiserSawCleanup);
	QVERIFY(rejectedAfterCancel);
	QVERIFY(reply);
	QCOMPARE(reply->tag.kind, ReadKind::Mdb);
	const auto *report = std::get_if<Bella::MdbReport>(&reply->report);
	QVERIFY(report);
	QCOMPARE(report->reading, Bella::MdbReadResult::Cancelled);
}

// MARK: - Physical rechecks and progress

void TestBellaScanner::physicalRecheck_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<PhysicalCheck>("expected");
	QTest::newRow("same size and date") << 0 << PhysicalCheck::Verified;
	QTest::newRow("empty file size is available") << 1 << PhysicalCheck::Verified;
	QTest::newRow("present without a current date") << 2 << PhysicalCheck::Unverified;
	QTest::newRow("present without a discovery size") << 3 << PhysicalCheck::Unverified;
	QTest::newRow("size differs even though date is missing") << 4 << PhysicalCheck::Changed;
	QTest::newRow("date differs even though size is missing") << 5 << PhysicalCheck::Changed;
	QTest::newRow("presence could not be confirmed") << 6 << PhysicalCheck::Unavailable;
	QTest::newRow("regular file was replaced by a different entry type") << 7 << PhysicalCheck::Changed;
}

void TestBellaScanner::physicalRecheck()
{
	QFETCH(int, variation);
	QFETCH(PhysicalCheck, expected);
	Bella::ScanEntry entry;
	entry.sizeBytes = 100;
	entry.modified = QDateTime::fromMSecsSinceEpoch(1000, Qt::UTC);
	Bella::Detail::PhysicalObservation observed{PhysicalPresence::File, entry.sizeBytes, entry.modified};
	switch (variation)
	{
	case 1:
		entry.sizeBytes = 0;
		observed.sizeBytes = 0;
		break;
	case 2:
		observed.modified = {};
		break;
	case 3:
		entry.sizeBytes.reset();
		break;
	case 4:
		observed.sizeBytes = 120;
		observed.modified = {};
		break;
	case 5:
		observed.sizeBytes.reset();
		observed.modified = entry.modified.addSecs(1);
		break;
	case 6:
		observed = {};
		break;
	case 7:
		observed = {PhysicalPresence::Replaced, {}, {}};
		break;
	default:
		break;
	}
	QCOMPARE(Bella::Detail::comparePhysical(entry, observed), expected);
}

void TestBellaScanner::completedStageProgress_data()
{
	QTest::addColumn<bool>("cancelLastHeader");
	QTest::newRow("completed replies") << false;
	QTest::newRow("one header left unsubmitted") << true;
}

void TestBellaScanner::completedStageProgress()
{
	QFETCH(bool, cancelLastHeader);
	using Stage = Bella::ScanStage;
	struct Progress
	{
		Stage stage;
		qsizetype completed;
		qsizetype total;
		QString path;
		bool databaseWarningDelivered;
	};

	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = pathIn(temporary, QStringLiteral("OMFI MediaFiles"));
	QVERIFY(!root.isEmpty());
	const auto database = root + QStringLiteral("/msmFMID.pmr");
	const auto first = root + QStringLiteral("/First.wav");
	const auto last = root + QStringLiteral("/Last.wav");
	QVERIFY(writeFile(database)); // Unreadable PMR produces a warning during collection.
	QVERIFY(writeFile(first, pcmWave(4)));
	QVERIFY(writeFile(last, pcmWave(8)));

	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	QVector<Progress> progress;
	bool databaseWarningDelivered = false;
	callbacks.warning = [&](const Bella::ScanProblem &problem)
	{
		if (problem.path == database)
			databaseWarningDelivered = true;
	};
	callbacks.progress = [&](qsizetype, qsizetype, const QString &path)
	{
		if (cancelLastHeader && path == last)
			control.cancel();
	};
	callbacks.stageProgress = [&](Stage stage, qsizetype completed, qsizetype total, const QString &path)
	{
		progress.append({stage, completed, total, path, databaseWarningDelivered});
	};
	const auto result = Bella::scan(treeRequest(root, true), control, callbacks);
	QCOMPARE(result.outcome, cancelLastHeader ? Bella::ScanOutcome::Cancelled : Bella::ScanOutcome::Completed);
	QVERIFY(result.discoveryComplete);
	QCOMPARE(progress.size(), cancelLastHeader ? qsizetype(5) : qsizetype(6));
	const auto expect = [&](qsizetype index, Stage stage, qsizetype completed, qsizetype total)
	{
		QCOMPARE(progress.at(index).stage, stage);
		QCOMPARE(progress.at(index).completed, completed);
		QCOMPARE(progress.at(index).total, total);
	};
	expect(0, Stage::ReadingDatabases, 0, 1);
	QVERIFY(!progress.at(0).databaseWarningDelivered);
	expect(1, Stage::ReadingDatabases, 1, 1);
	QCOMPARE(progress.at(1).path, database);
	QVERIFY(progress.at(1).databaseWarningDelivered);
	expect(2, Stage::CheckingFiles, 0, 0);
	expect(3, Stage::ReadingHeaders, 0, 2);
	expect(4, Stage::ReadingHeaders, 1, 2);
	if (cancelLastHeader)
		QCOMPARE(progress.at(4).path, first);
	else
	{
		expect(5, Stage::ReadingHeaders, 2, 2);
		QStringList paths{progress.at(4).path, progress.at(5).path};
		paths.sort();
		const QStringList expected{first, last};
		QCOMPARE(paths, expected);
	}
}

// MARK: - Identity handoff and retained evidence

void TestBellaScanner::matchesAfterHeader_data()
{
	QTest::addColumn<bool>("replace");
	QTest::newRow("stable native header supplies timing") << false;
	QTest::newRow("new valid header is excluded after a discovery mismatch") << true;
}

void TestBellaScanner::matchesAfterHeader()
{
	QFETCH(bool, replace);
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = pathIn(temporary, QStringLiteral("OMFI MediaFiles"));
	QVERIFY(!root.isEmpty());
	const auto path = root + QStringLiteral("/Tone.wav");
	const auto original = pcmWave(4);
	QVERIFY(writeFile(path, original));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	QStringList submissions;
	bool replacementWritten = false;
	callbacks.progress = [&](qsizetype, qsizetype, const QString &source)
	{
		submissions.append(source);
		// The first matching pass has finished. The replacement stays stable
		// throughout its own successful header read.
		if (replace && source == path)
			replacementWritten = writeFile(path, pcmWave(8));
	};
	const auto result = Bella::scan(treeRequest(root, true), control, callbacks);
	QCOMPARE(result.outcome, Bella::ScanOutcome::Completed);
	QVERIFY(result.discoveryComplete);
	QCOMPARE(result.entries.size(), qsizetype(1));
	QCOMPARE(submissions, QStringList{path});
	QCOMPARE(replacementWritten, replace);
	const auto &entry = result.entries[0];
	QCOMPARE(entry.path, path);
	QCOMPARE(entry.sizeBytes, std::optional<qint64>(original.size()));
	QCOMPARE(entry.match.localReference, Bella::LocalReference::NoDatabase);
	QVERIFY(!entry.match.needsHeader);
	const auto &metadata = entry.match.metadata;
	if (replace)
	{
		const auto *reason = std::get_if<Bella::RescanReason>(&entry.operationSafety);
		QVERIFY(reason);
		QCOMPARE(*reason, Bella::RescanReason::FileChanged);
		QCOMPARE(metadata.kind, Bella::MatchKind::Unknown);
		QVERIFY(!metadata.sampleRate);
		QVERIFY(!metadata.timing.duration);
		QVERIFY(std::any_of(result.problems.cbegin(), result.problems.cend(), [&](const auto &problem)
							{ return problem.path == path && problem.reason.contains(QStringLiteral("changed after discovery"), Qt::CaseInsensitive); }));
	}
	else
	{
		QVERIFY(std::holds_alternative<Bella::CheckPhysicalFile>(entry.operationSafety));
		QVERIFY(result.problems.isEmpty());
		QCOMPARE(metadata.kind, Bella::MatchKind::Audio);
		QCOMPARE(metadata.bitDepth.bits, std::optional<quint32>(16));
		QCOMPARE(metadata.compression.compressionName, QStringLiteral("PCM"));
		QVERIFY(metadata.timing.duration);
		QCOMPARE(metadata.timing.duration->count, qint64(4));
		QCOMPARE(metadata.timing.duration->rate.numerator, quint64(48000));
		QCOMPARE(metadata.timing.duration->rate.denominator, quint64(1));
		QVERIFY(!metadata.timing.display);
	}
}

void TestBellaScanner::operationFileIdOrigin_data()
{
	QTest::addColumn<IdentityCase>("scenario");
	QTest::newRow("database match skips header") << IdentityCase::Database;
	QTest::newRow("attempted header confirms the full ID") << IdentityCase::Header;
	QTest::newRow("attempted header supplies no usable ID") << IdentityCase::HeaderWithoutId;
	QTest::newRow("cancel before required header preserves qualified database ID") << IdentityCase::CancelBeforeHeader;
	QTest::newRow("cancel before post-header recheck requires live physical checks") << IdentityCase::CancelBeforeRecheck;
	QTest::newRow("all-zero PMR ID does not become a database expectation") << IdentityCase::CancelWithZeroId;
}

void TestBellaScanner::operationFileIdOrigin()
{
	QFETCH(IdentityCase, scenario);
	const bool zeroId = scenario == IdentityCase::CancelWithZeroId;
	const bool databaseDescriptor = scenario == IdentityCase::Database;
	const bool cancelBeforeHeader = scenario == IdentityCase::CancelBeforeHeader || zeroId;
	const bool cancelAfterHeader = scenario == IdentityCase::CancelBeforeRecheck;
	const bool headerWithoutId = scenario == IdentityCase::HeaderWithoutId;
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = pathIn(temporary, QStringLiteral("OMFI MediaFiles"));
	QVERIFY(!root.isEmpty());
	const auto path = root + QStringLiteral("/Take.omf");
	const auto pmr = root + QStringLiteral("/msmFMID.pmr");
	const auto mdb = root + QStringLiteral("/msmMMOB.mdb");
	// Version 1 supplies the Bento owner's legacy ID. Version 8 can store a
	// complete all-zero 32-byte value without the nonzero legacy wrapper.
	auto pmrBytes = QByteArray::fromHex(zeroId ? "a90700000800000001000000" : "a90700000100000001000000");
	pmrBytes += zeroId ? QByteArray(32, '\0') : QByteArray::fromHex("0b00000084030000");
	pmrBytes += QByteArray::fromHex("0800") + QByteArray("Take.omf");
	if (zeroId)
		pmrBytes += QByteArray(2 + 32, '\0'); // Empty Project and zero master ID.
	pmrBytes += QByteArray(4, '\0');		  // Ignored modification word.
	QVERIFY(writeFile(pmr, pmrBytes));
	QVERIFY(writeFile(path, operationFileBento(!headerWithoutId && !zeroId, cancelAfterHeader ? -1 : 250)));
	if (databaseDescriptor)
		QVERIFY(writeFile(mdb, operationFileBento(false)));

	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	QStringList submissions;
	callbacks.progress = [&](qsizetype, qsizetype, const QString &source)
	{
		submissions.append(source);
		if (cancelBeforeHeader && source == path)
			control.cancel();
	};
	bool headerWarning = false;
	callbacks.warning = [&](const Bella::ScanProblem &problem)
	{
		// A local Length problem does not invalidate the header's own-file ID.
		// Cancel in the reply callback before the organiser rechecks the file.
		if (cancelAfterHeader && problem.path == path && problem.reason.contains(QStringLiteral("Negative length is unavailable.")))
		{
			headerWarning = true;
			control.cancel();
		}
	};
	const auto result = Bella::scan(treeRequest(root, true), control, callbacks);
	const bool cancelled = cancelBeforeHeader || cancelAfterHeader;
	QCOMPARE(result.outcome, cancelled ? Bella::ScanOutcome::Cancelled : Bella::ScanOutcome::Completed);
	QVERIFY(result.discoveryComplete);
	const QStringList expectedSubmissions{pmr, databaseDescriptor ? mdb : path};
	QCOMPARE(submissions, expectedSubmissions);
	QCOMPARE(headerWarning, cancelAfterHeader);
	QCOMPARE(result.entries.size(), qsizetype(1));
	const auto &entry = result.entries.first();
	QCOMPARE(entry.path, path);
	QCOMPARE(entry.match.localReference, Bella::LocalReference::Listed);
	const auto expectedId = zeroId ? QByteArray(32, '\0') : QByteArray::fromHex("060a2b340101010101010f00130000000b00000084030000060e2b347f7f2a80");
	QVERIFY(entry.match.metadata.fileMobId);
	QCOMPARE(entry.match.metadata.fileMobId->bytes().toByteArray(), expectedId);
	if (headerWithoutId)
	{
		QVERIFY(!entry.match.needsHeader);
		QVERIFY(std::any_of(result.problems.cbegin(), result.problems.cend(), [&](const auto &problem)
							{ return problem.path == path && problem.reason.contains(QStringLiteral("physical file identity")); }));
	}
	if (cancelAfterHeader || headerWithoutId || zeroId)
		QVERIFY(std::holds_alternative<Bella::CheckPhysicalFile>(entry.operationSafety));
	else
	{
		const auto *identity = std::get_if<Bella::VerifyFileId>(&entry.operationSafety);
		QVERIFY(identity);
		QCOMPARE(identity->id.bytes().toByteArray(), expectedId);
		QCOMPARE(identity->origin, databaseDescriptor || cancelBeforeHeader ? Bella::FileIdOrigin::Database : Bella::FileIdOrigin::Header);
	}
}

void TestBellaScanner::unconfirmedPhysicalFileIsKept()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto root = pathIn(temporary, QStringLiteral("OMFI MediaFiles"));
	QVERIFY(!root.isEmpty());
	const auto path = root + QStringLiteral("/Tone.wav");
	const auto database = root + QStringLiteral("/msmFMID.pmr");
	QVERIFY(writeFile(path, pcmWave(4)));
	// Valid, empty v8 PMR. Its reference coverage cannot compensate for a
	// physical file whose presence is unconfirmed at the first recheck.
	QVERIFY(writeFile(database, QByteArray::fromHex("a90700000800000000000000")));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	bool removed = false;
	QStringList submissions;
	callbacks.progress = [&](qsizetype, qsizetype, const QString &source)
	{
		submissions.append(source);
		if (source == database)
			removed = QFile::remove(path);
	};
	const auto result = Bella::scan(treeRequest(root, true), control, callbacks);
	QVERIFY(removed);
	QCOMPARE(result.outcome, Bella::ScanOutcome::Completed);
	QVERIFY(result.discoveryComplete);
	QCOMPARE(submissions, QStringList{database});
	QCOMPARE(result.entries.size(), qsizetype(1));
	const auto &entry = result.entries[0];
	QCOMPARE(entry.path, path);
	const auto *reason = std::get_if<Bella::RescanReason>(&entry.operationSafety);
	QVERIFY(reason);
	QCOMPARE(*reason, Bella::RescanReason::FilePresenceUnconfirmed);
	QCOMPARE(entry.match.localReference, Bella::LocalReference::Unresolved);
	QVERIFY(!entry.match.needsHeader);
	QVERIFY(!entry.match.metadata.timing.duration);
	QVERIFY(std::any_of(result.problems.cbegin(), result.problems.cend(), [&](const auto &problem)
						{ return problem.path == path && problem.reason.contains(QStringLiteral("could not confirm"), Qt::CaseInsensitive); }));
}

// MARK: - Cancellation and callback failures

void TestBellaScanner::cancelKeepsKnownUnreadDatabase()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto tree = pathIn(temporary, QStringLiteral("Avid MediaFiles"));
	QVERIFY(!tree.isEmpty());
	const auto root = tree + QStringLiteral("/MXF");
	const auto withDatabase = root + QStringLiteral("/1/With.mxf");
	const auto withoutDatabase = root + QStringLiteral("/2/Without.mxf");
	const auto database = root + QStringLiteral("/1/msmMMOB.mdb");
	QVERIFY(writeFile(withDatabase));
	QVERIFY(writeFile(withoutDatabase));
	QVERIFY(writeFile(database));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	QStringList submissions;
	callbacks.progress = [&](qsizetype, qsizetype, const QString &source)
	{
		submissions.append(source);
		if (source == database)
			control.cancel();
	};
	const auto result = Bella::scan(treeRequest(tree), control, callbacks);
	QCOMPARE(result.outcome, Bella::ScanOutcome::Cancelled);
	QVERIFY(result.discoveryComplete);
	QVERIFY(result.problems.isEmpty());
	QCOMPARE(submissions, QStringList{database});
	QCOMPARE(result.entries.size(), qsizetype(2));
	const auto *known = entryAt(result, withDatabase);
	const auto *absent = entryAt(result, withoutDatabase);
	QVERIFY(known);
	QVERIFY(absent);
	QVERIFY(std::holds_alternative<Bella::CheckPhysicalFile>(known->operationSafety));
	QVERIFY(std::holds_alternative<Bella::CheckPhysicalFile>(absent->operationSafety));
	QCOMPARE(known->match.localReference, Bella::LocalReference::Unresolved);
	QCOMPARE(absent->match.localReference, Bella::LocalReference::NoDatabase);
	QVERIFY(!known->match.metadata.fileMobId);
	QVERIFY(!known->match.needsHeader);
	QVERIFY(!absent->match.needsHeader);
}

void TestBellaScanner::discoveryCallbackFailureIsIncomplete()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto tree = pathIn(temporary, QStringLiteral("Avid MediaFiles"));
	QVERIFY(!tree.isEmpty());
	const auto retained = tree + QStringLiteral("/MXF/1/Retained.mxf");
	const auto next = tree + QStringLiteral("/MXF/2");
	QVERIFY(writeFile(retained));
	QVERIFY(writeFile(next + QStringLiteral("/Unvisited.mxf")));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	callbacks.discovering = [&](const QString &path)
	{
		if (path == next)
			throw std::runtime_error("authored discovery callback failure");
	};
	const auto result = Bella::scan(treeRequest(tree), control, callbacks);
	QCOMPARE(result.outcome, Bella::ScanOutcome::Failed);
	QVERIFY(!result.discoveryComplete);
	QCOMPARE(result.entries.size(), qsizetype(1));
	QCOMPARE(result.entries[0].path, retained);
	QVERIFY(std::any_of(result.problems.cbegin(), result.problems.cend(), [](const auto &problem)
						{ return problem.reason.contains(QStringLiteral("authored discovery callback failure")); }));
}

void TestBellaScanner::organiserCallbackFailureStopsWithOutstandingWork()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto tree = pathIn(temporary, QStringLiteral("Avid MediaFiles"));
	QVERIFY(!tree.isEmpty());
	const auto folder = tree + QStringLiteral("/MXF/1");
	const auto path = folder + QStringLiteral("/Retained.mxf");
	const auto first = folder + QStringLiteral("/amaFMID.pmr");
	const auto second = folder + QStringLiteral("/msmFMID.pmr");
	QVERIFY(writeFile(path));
	const auto emptyPmr = QByteArray::fromHex("a90700000800000000000000");
	QVERIFY(writeFile(first, emptyPmr));
	QVERIFY(writeFile(second, emptyPmr));
	Bella::ScanControl control;
	Bella::ScanCallbacks callbacks;
	QStringList paths;
	QVector<qsizetype> submittedCounts;
	callbacks.progress = [&](qsizetype submitted, qsizetype, const QString &source)
	{
		paths.append(source);
		submittedCounts.append(submitted);
		// The first submission still owes collection even if its reader finished.
		if (source == second)
			throw std::runtime_error("authored progress callback failure");
	};
	const auto result = Bella::scan(treeRequest(tree), control, callbacks);
	QCOMPARE(result.outcome, Bella::ScanOutcome::Failed);
	QVERIFY(result.discoveryComplete);
	QVERIFY(control.cancelled());
	const QStringList expectedPaths{first, second};
	const QVector<qsizetype> expectedCounts{0, 1};
	QCOMPARE(paths, expectedPaths);
	QCOMPARE(submittedCounts, expectedCounts);
	QCOMPARE(result.entries.size(), qsizetype(1));
	QCOMPARE(result.entries[0].path, path);
	QVERIFY(std::any_of(result.problems.cbegin(), result.problems.cend(), [](const auto &problem)
						{ return problem.reason.contains(QStringLiteral("authored progress callback failure")); }));
}

QTEST_GUILESS_MAIN(TestBellaScanner)
#include "tst_bella_scanner.moc"
