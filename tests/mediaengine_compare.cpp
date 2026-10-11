// Read-only scan probe. Each fresh process fingerprints the retained metadata
// and measures scan time/RAM before producing proof hashes and CSV rows.
// Only the requested report and CSV are written.
#include "mediaenginefingerprint.h"
#include "mediaengine/scanengine.h"
#include "mediaengine/discoveryengine.h"
#include "mediaengineadapter.h"
#include "mediacsv.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
	using MediaEngineProof::Fingerprint;
	using MediaEngineProof::SnapshotIds;

	QJsonObject memory()
	{
		QJsonObject result;
#ifdef Q_OS_MACOS
		task_vm_info_data_t info{};
		mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
		if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
		{
			result.insert(QStringLiteral("residentBytes"), qint64(info.resident_size));
			result.insert(QStringLiteral("physicalFootprintBytes"), qint64(info.phys_footprint));
		}
#elif defined(Q_OS_WIN)
		PROCESS_MEMORY_COUNTERS_EX info{};
		info.cb = sizeof(info);
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&info), sizeof(info)))
		{
			result.insert(QStringLiteral("residentBytes"), qint64(info.WorkingSetSize));
			result.insert(QStringLiteral("peakResidentBytes"), qint64(info.PeakWorkingSetSize));
			result.insert(QStringLiteral("privateBytes"), qint64(info.PrivateUsage));
		}
#elif defined(Q_OS_LINUX)
		QFile statm(QStringLiteral("/proc/self/statm"));
		if (statm.open(QIODevice::ReadOnly))
		{
			const auto fields = statm.readAll().simplified().split(' ');
			if (fields.size() >= 2)
				result.insert(QStringLiteral("residentBytes"), fields[1].toLongLong() * qint64(sysconf(_SC_PAGESIZE)));
		}
#endif
#ifdef Q_OS_UNIX
		rusage usage{};
		if (getrusage(RUSAGE_SELF, &usage) == 0)
		{
#ifdef Q_OS_MACOS
			result.insert(QStringLiteral("peakResidentBytes"), qint64(usage.ru_maxrss));
#else
			result.insert(QStringLiteral("peakResidentBytes"), qint64(usage.ru_maxrss) * 1024);
#endif
		}
#endif
		return result;
	}

	QJsonObject stamp(const QString &path)
	{
		QJsonObject result{{QStringLiteral("path"), path}};
#ifdef Q_OS_UNIX
		struct stat info{};
		if (::stat(QFile::encodeName(path).constData(), &info) != 0)
		{
			result.insert(QStringLiteral("available"), false);
			return result;
		}
		result.insert(QStringLiteral("available"), true);
		result.insert(QStringLiteral("device"), QString::number(quint64(info.st_dev)));
		result.insert(QStringLiteral("inode"), QString::number(quint64(info.st_ino)));
		result.insert(QStringLiteral("sizeBytes"), QString::number(qint64(info.st_size)));
		result.insert(QStringLiteral("directory"), bool(S_ISDIR(info.st_mode)));
#ifdef Q_OS_MACOS
		result.insert(QStringLiteral("mtimeSeconds"), QString::number(qint64(info.st_mtimespec.tv_sec)));
		result.insert(QStringLiteral("mtimeNanoseconds"), QString::number(qint64(info.st_mtimespec.tv_nsec)));
#else
		result.insert(QStringLiteral("mtimeSeconds"), QString::number(qint64(info.st_mtim.tv_sec)));
		result.insert(QStringLiteral("mtimeNanoseconds"), QString::number(qint64(info.st_mtim.tv_nsec)));
#endif
#else
		std::error_code error;
		const auto nativePath = QDir(path).filesystemPath();
		const auto status = std::filesystem::status(nativePath, error);
		if (error || !std::filesystem::exists(status))
		{
			result.insert(QStringLiteral("available"), false);
			return result;
		}
		result.insert(QStringLiteral("available"), true);
		const bool directory = std::filesystem::is_directory(status);
		result.insert(QStringLiteral("directory"), directory);
		if (!directory)
		{
			const auto size = std::filesystem::file_size(nativePath, error);
			if (!error)
				result.insert(QStringLiteral("sizeBytes"), QString::number(quint64(size)));
		}
		const auto modified = std::filesystem::last_write_time(nativePath, error);
		if (!error)
			result.insert(QStringLiteral("fileClockTicks"), QString::number(qint64(modified.time_since_epoch().count())));
#endif
		return result;
	}

	QStringList inputPaths(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation)
	{
		QSet<QString> paths;
		// Preflight discovery records the input stamp inventory. Its temporary
		// rows are destroyed before the baseline and retained RAM samples.
		const auto discovery = MediaEngine::DiscoveryEngine{}.discover(request, cancellation,
			[&](const QString &path) { paths.insert(path); });
		for (const auto &candidate : discovery.candidates)
		{
			paths.insert(candidate.path);
			paths.insert(QFileInfo(candidate.path).absolutePath());
		}
		auto result = paths.values();
		std::sort(result.begin(), result.end());
		return result;
	}
	QJsonArray stamps(const QStringList &paths)
	{
		QJsonArray result;
		for (const auto &path : paths)
			result.append(stamp(path));
		return result;
	}

	QByteArray fileDigest(const QString &path)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			throw std::runtime_error(QStringLiteral("Cannot hash %1: %2").arg(path, file.errorString()).toStdString());
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!hash.addData(&file))
			throw std::runtime_error(QStringLiteral("Cannot finish hashing %1").arg(path).toStdString());
		return hash.result().toHex();
	}

	bool writeReport(const QString &path, const QJsonObject &report)
	{
		const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
		if (path == QLatin1String("-"))
			return std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout) == size_t(bytes.size());
		QSaveFile output(path);
		return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
	}
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("mediaengine_compare"));
	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Read-only MediaEngine metadata, evidence and process-memory scan proof"));
	parser.addHelpOption();
	const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("JSON report path; - writes stdout"), QStringLiteral("path"), QStringLiteral("-"));
	const QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("Optional app-boundary CSV path"), QStringLiteral("path"));
	const QCommandLineOption expectedRowsOption(QStringLiteral("expected-rows"), QStringLiteral("Require this physical row count"), QStringLiteral("count"));
	const QCommandLineOption noOmfOption(QStringLiteral("no-omf"), QStringLiteral("Disable OMF-family discovery"));
	parser.addOptions({outputOption, csvOption, expectedRowsOption, noOmfOption});
	parser.addPositionalArgument(QStringLiteral("roots"), QStringLiteral("One or more managed roots or direct containing bases"), QStringLiteral("roots..."));
	parser.process(app);
	if (parser.positionalArguments().isEmpty())
		parser.showHelp(1);
	bool validRows = true;
	const qint64 expectedRows = parser.isSet(expectedRowsOption) ? parser.value(expectedRowsOption).toLongLong(&validRows) : -1;
	if (!validRows || (parser.isSet(expectedRowsOption) && expectedRows < 0))
		parser.showHelp(1);

	QJsonObject report{{QStringLiteral("schemaVersion"), 2},
		{QStringLiteral("verificationMode"), QStringLiteral("scan")},
		{QStringLiteral("sourceVerificationPerformed"), false}};
	QStringList errors;
	try
	{
		const MediaEngine::ScanRequest request{parser.positionalArguments(), !parser.isSet(noOmfOption)};
		const MediaEngine::Cancellation cancellation;
		const auto paths = inputPaths(request, cancellation);
		const auto before = stamps(paths);
		Fingerprint callbacks;
		qint64 progressCalls = 0, finalisingCalls = 0, warningCalls = 0, discoveringCalls = 0, readingCalls = 0;
		MediaEngine::ScanCallbacks observers;
		observers.progress = [&](int current, int total, const QString &path) {
			++progressCalls;
			callbacks.stream << qint32(0) << current << total;
			callbacks.text(path);
		};
		observers.finalising = [&] { ++finalisingCalls; callbacks.stream << qint32(1); };
		observers.warning = [&](const QString &message) {
			++warningCalls;
			callbacks.stream << qint32(2);
			callbacks.text(message);
		};
		observers.discovering = [&](const QString &path) {
			++discoveringCalls;
			callbacks.stream << qint32(3);
			callbacks.text(path);
		};
		observers.reading = [&](const MediaEngine::SourceCandidate &candidate) {
			++readingCalls;
			callbacks.stream << qint32(4);
			callbacks.candidate(candidate);
		};
		const auto memoryBefore = memory();
		QElapsedTimer timer;
		timer.start();
		const auto scan = QSharedPointer<MediaEngine::ScanResult>::create(
			MediaEngine::ScanEngine{}.scan(request, cancellation, observers));
		const auto scanMs = timer.elapsed();
		// These samples precede fingerprints and adapter/CSV rows. The scan
		// session stays retained so its metadata and evidence are included.
		const auto memoryRetained = memory();
		const auto afterScan = stamps(paths);
		timer.restart();
		SnapshotIds snapshots;
		snapshots.seed(*scan);
		Fingerprint filesProof(&snapshots), scheduleProof(&snapshots), issuesProof(&snapshots), stateProof;
		filesProof.stream << qint64(scan->files.size());
		QJsonArray fileDigests;
		for (const auto &file : scan->files)
		{
			Fingerprint proof(&snapshots);
			proof.file(file);
			const auto digest = proof.result();
			filesProof.text(file.path);
			filesProof.bytes(digest);
			fileDigests.append(QJsonObject{{QStringLiteral("path"), file.path},
				{QStringLiteral("kelpieId"), QString::number(file.kelpieId)},
				{QStringLiteral("sha256"), QString::fromLatin1(digest)},
				{QStringLiteral("serializedBytes"), proof.sink.serializedBytes}});
		}
		// Receipt aliases have a stable order across files, issues and scheduling.
		issuesProof.stream << qint64(scan->discoveryIssues.size());
		for (const auto &issue : scan->discoveryIssues)
		{
			issuesProof.stream << qint32(issue.kind);
			issuesProof.text(issue.path);
			issuesProof.text(issue.explanation);
		}
		issuesProof.stream << qint64(scan->reconciliationIssues.size());
		for (const auto &issue : scan->reconciliationIssues)
		{
			issuesProof.stream << qint32(issue.kind);
			issuesProof.snapshot(issue.source);
			issuesProof.text(issue.expectedPath);
			issuesProof.text(issue.fileMobId);
			issuesProof.texts(issue.matchingPaths);
			issuesProof.stream << issue.scopeComplete;
			issuesProof.text(issue.explanation);
		}
		scheduleProof.stream << qint64(scan->candidates.size()) << qint64(scan->sources.size());
		if (scan->sources.size() != scan->candidates.size())
			throw std::runtime_error("Candidate/source count differs");
		QJsonArray sourceDigests;
		QMap<QString, qint64> reasons;
		qint64 headersRead = 0, headersSkipped = 0, databasesRead = 0;
		for (qsizetype index = 0; index < scan->sources.size(); ++index)
		{
			const auto &receipt = scan->sources[index];
			const auto &candidate = scan->candidates[index];
			const bool database = candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Pmr || candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mdb;
			++reasons[receipt.readReason];
			if (database)
				databasesRead += receipt.outcome != MediaEngine::ParsedSource::Outcome::NotRead;
			else if (receipt.outcome == MediaEngine::ParsedSource::Outcome::NotRead)
				++headersSkipped;
			else
				++headersRead;
			const QJsonObject details{{QStringLiteral("path"), candidate.path}, {QStringLiteral("hint"), int(candidate.hint)},
				{QStringLiteral("outcome"), int(receipt.outcome)}, {QStringLiteral("readReason"), receipt.readReason}};
			sourceDigests.append(details);
			scheduleProof.stream << qint64(index);
			scheduleProof.candidate(candidate);
			scheduleProof.sourceReceipt(receipt);
		}
		stateProof.texts(scan->request.roots);
		stateProof.stream << scan->request.omfScan << scan->discoveryComplete << scan->cancelled
			<< scan->parsingComplete << scan->reconciliationComplete;
		QJsonObject reasonCounts;
		for (auto reason = reasons.cbegin(); reason != reasons.cend(); ++reason)
			reasonCounts.insert(reason.key(), reason.value());
		const QJsonObject hashes{{QStringLiteral("filesSha256"), QString::fromLatin1(filesProof.result())},
			{QStringLiteral("schedulingSha256"), QString::fromLatin1(scheduleProof.result())},
			{QStringLiteral("issuesSha256"), QString::fromLatin1(issuesProof.result())},
			{QStringLiteral("stateSha256"), QString::fromLatin1(stateProof.result())},
			{QStringLiteral("callbacksSha256"), QString::fromLatin1(callbacks.result())}};
		const auto verificationMs = timer.elapsed();
		if (parser.isSet(csvOption))
		{
			QVector<MediaFile> rows;
			rows.reserve(scan->files.size());
			for (const auto &file : scan->files)
				rows.append(mediaEngineMediaFile(file, scan));
			if (!MediaCsv::write(parser.value(csvOption), rows, {true, true}))
				throw std::runtime_error("CSV write failed");
			report.insert(QStringLiteral("csv"), parser.value(csvOption));
			report.insert(QStringLiteral("csvSha256"), QString::fromLatin1(fileDigest(parser.value(csvOption))));
		}
		const auto afterVerification = stamps(paths);
		const bool stable = before == afterScan && before == afterVerification;
		if (!stable)
			errors.append(QStringLiteral("Source or folder stamps changed during scan/verification"));
		if (expectedRows >= 0 && scan->files.size() != expectedRows)
			errors.append(QStringLiteral("Expected %1 rows; found %2").arg(expectedRows).arg(scan->files.size()));
		const bool complete = scan->discoveryComplete && scan->parsingComplete && scan->reconciliationComplete && !scan->cancelled;
		if (!complete)
			errors.append(QStringLiteral("Scan did not complete discovery, parsing and reconciliation"));
		report.insert(QStringLiteral("roots"), QJsonArray::fromStringList(scan->request.roots));
		report.insert(QStringLiteral("omfScan"), scan->request.omfScan);
		report.insert(QStringLiteral("rows"), scan->files.size());
		report.insert(QStringLiteral("candidates"), scan->candidates.size());
		report.insert(QStringLiteral("sources"), scan->sources.size());
		report.insert(QStringLiteral("discoveryIssues"), scan->discoveryIssues.size());
		report.insert(QStringLiteral("reconciliationIssues"), scan->reconciliationIssues.size());
		report.insert(QStringLiteral("discoveryComplete"), scan->discoveryComplete);
		report.insert(QStringLiteral("parsingComplete"), scan->parsingComplete);
		report.insert(QStringLiteral("reconciliationComplete"), scan->reconciliationComplete);
		report.insert(QStringLiteral("cancelled"), scan->cancelled);
		report.insert(QStringLiteral("scanMs"), scanMs);
		report.insert(QStringLiteral("verificationMs"), verificationMs);
		report.insert(QStringLiteral("memoryBefore"), memoryBefore);
		report.insert(QStringLiteral("memoryRetained"), memoryRetained);
		report.insert(QStringLiteral("snapshotIdentityCount"), snapshots.size());
		report.insert(QStringLiteral("headerReads"), headersRead);
		report.insert(QStringLiteral("headerSkips"), headersSkipped);
		report.insert(QStringLiteral("databaseReads"), databasesRead);
		report.insert(QStringLiteral("readReasonCounts"), reasonCounts);
		report.insert(QStringLiteral("hashes"), hashes);
		report.insert(QStringLiteral("fileDigests"), fileDigests);
		report.insert(QStringLiteral("sourceDigests"), sourceDigests);
		report.insert(QStringLiteral("callbackCounts"), QJsonObject{{QStringLiteral("progress"), progressCalls},
			{QStringLiteral("finalising"), finalisingCalls}, {QStringLiteral("warning"), warningCalls},
			{QStringLiteral("discovering"), discoveringCalls}, {QStringLiteral("reading"), readingCalls}});
		report.insert(QStringLiteral("inputStampsBefore"), before);
		report.insert(QStringLiteral("inputStampsAfterScan"), afterScan);
		report.insert(QStringLiteral("inputStampsAfterVerification"), afterVerification);
		report.insert(QStringLiteral("sourceAndFolderStampsStable"), stable);
		report.insert(QStringLiteral("measurementScope"), QJsonObject{
			{QStringLiteral("scan"), QStringLiteral("Synchronous engine scan including discovery, readers, projection, matching and selection; excludes verification and CSV adapters.")},
			{QStringLiteral("memory"), QStringLiteral("Fresh-process current resident/physical footprint or Windows private bytes and peak resident sampled immediately after scan return, retaining session; before proof fingerprints and adapter rows. Peak includes process setup, preflight discovery and the scan. Unsupported counters are omitted.")},
			{QStringLiteral("setup"), QStringLiteral("Preflight discovery enumerates input stamps before timing. Its inventory is released before memoryBefore; stamps and bounded callback sink remain. Filesystem cache is uncontrolled; no cold-cache claim.")},
			{QStringLiteral("verification"), QStringLiteral("Hashes every final row's property fields, read coverage, observations, alternatives, selections and receipt aliases; scheduling receipts, stamps, issues, callbacks and completion flags. CSV and source/folder stamp checks also run. Dedicated format-reader tests verify parsed records separately.")},
			{QStringLiteral("csv"), QStringLiteral("Existing MediaEngine adapter and MediaCsv with precompute details and clip duration enabled; created after memory sample.")}});
	}
	catch (const std::exception &error)
	{
		errors.append(QString::fromUtf8(error.what()));
	}
	report.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
	report.insert(QStringLiteral("verificationPassed"), errors.isEmpty());
	if (!writeReport(parser.value(outputOption), report))
	{
		std::fprintf(stderr, "Cannot write comparison report\n");
		return 2;
	}
	for (const auto &error : errors)
		std::fprintf(stderr, "%s\n", error.toUtf8().constData());
	return errors.isEmpty() ? 0 : 3;
}
