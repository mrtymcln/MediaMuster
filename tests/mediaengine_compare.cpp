// Read-only comparison probe. Run one engine per fresh process and compare the
// semantic hashes/CSV externally. Only the requested report and CSV are written.
#include "mediaenginefingerprint.h"
#include "mediaengine/databasesource.h"
#include "mediaengine/mxfsource.h"
#include "mediaengine/scanengine.h"
#include "mediaengine/discoveryengine.h"
#include "mediaengine/omfreader.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/mxfreader.h"
#include "mediaengine/pmrreader.h"
#include "mediaengine/sourcearchive.h"
#include "mediaengineadapter.h"
#include "mediacsv.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDataStream>
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
#include <memory>

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
		// Setup discovery gives both engines the same initial stamp inventory. Its
		// temporary rows are destroyed before the baseline and retained RAM samples.
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

	QJsonObject checkImage(const MediaEngine::DatabaseImage &image, const QString &path)
	{
		QJsonObject result{{QStringLiteral("capturedBytes"), qint64(image.bytes().size())},
			{QStringLiteral("expectedSize"), image.expectedSize()},
			{QStringLiteral("acquisitionOutcome"), int(image.outcome())},
			{QStringLiteral("acquisitionComplete"), image.acquisitionComplete()},
			{QStringLiteral("diagnostics"), QJsonArray::fromStringList(image.diagnostics())}};
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			result.insert(QStringLiteral("exactOriginalBytes"), false);
			result.insert(QStringLiteral("error"), file.errorString());
			return result;
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		qint64 offset = 0;
		bool same = file.size() == image.bytes().size() && image.acquisitionComplete();
		while (!file.atEnd())
		{
			const auto chunk = file.read(1024 * 1024);
			if (chunk.isEmpty())
			{
				same = false;
				break;
			}
			hash.addData(chunk);
			if (offset > image.bytes().size() || chunk.size() > image.bytes().size() - offset ||
				std::memcmp(chunk.constData(), image.bytes().constData() + offset, size_t(chunk.size())) != 0)
				same = false;
			offset += chunk.size();
		}
		if (file.error() != QFileDevice::NoError)
		{
			same = false;
			result.insert(QStringLiteral("error"), file.errorString());
		}
		same = same && offset == image.bytes().size() && image.expectedSize() == offset;
		result.insert(QStringLiteral("exactOriginalBytes"), same);
		result.insert(QStringLiteral("originalFileSha256"), QString::fromLatin1(hash.result().toHex()));
		result.insert(QStringLiteral("imageSha256"), QString::fromLatin1(
			QCryptographicHash::hash(image.bytes(), QCryptographicHash::Sha256).toHex()));
		return result;
	}

	QJsonObject mxfImageReceipt(const MediaEngine::MxfImage &image)
	{
		// Array capacity exposes spare byte storage; it excludes index, container
		// and allocator overhead, which the process-memory samples also measure.
		qint64 capacityBytes = 0;
		for (const auto &range : image.ranges())
			capacityBytes += range.bytes.capacity();
		return QJsonObject{{QStringLiteral("physicalSize"), image.physicalSize()},
			{QStringLiteral("acquiredBytes"), image.acquiredBytes()},
			{QStringLiteral("capturedBytes"), image.storedBytes()},
			{QStringLiteral("rangeCapacityBytes"), capacityBytes},
			{QStringLiteral("rangeCount"), image.rangeCount()},
			{QStringLiteral("valid"), image.valid()},
			{QStringLiteral("diagnostics"), QJsonArray::fromStringList(image.diagnostics())}};
	}

	QJsonObject checkMxfImage(const MediaEngine::MxfImage &image, const QString &path)
	{
		auto result = mxfImageReceipt(image);
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			result.insert(QStringLiteral("exactOriginalRanges"), false);
			result.insert(QStringLiteral("error"), file.errorString());
			return result;
		}
		// Compare acquired regions directly. Hash their original offsets and lengths
		// as well as bytes, so identical bytes at a different location cannot pass.
		QCryptographicHash originalHash(QCryptographicHash::Sha256), imageHash(QCryptographicHash::Sha256);
		QJsonArray ranges;
		bool wellFormed = image.valid() && image.physicalSize() == file.size();
		bool exact = wellFormed;
		qint64 previousEnd = 0, storedBytes = 0;
		for (const auto &range : image.ranges())
		{
			const qint64 length = range.bytes.size();
			const bool bounded = range.offset >= previousEnd && length > 0 && range.offset <= image.physicalSize()
				&& length <= image.physicalSize() - range.offset;
			wellFormed = wellFormed && bounded;
			QByteArray framing;
			QDataStream framingStream(&framing, QIODevice::WriteOnly);
			framingStream << range.offset << length;
			originalHash.addData(framing);
			imageHash.addData(framing);
			imageHash.addData(range.bytes);
			QCryptographicHash originalRangeHash(QCryptographicHash::Sha256);
			bool same = bounded && file.seek(range.offset);
			qint64 obtained = 0;
			if (same)
			{
				while (obtained < length)
				{
					const auto chunk = file.read(std::min<qint64>(1024 * 1024, length - obtained));
					if (chunk.isEmpty())
					{
						same = false;
						break;
					}
					originalHash.addData(chunk);
					originalRangeHash.addData(chunk);
					if (std::memcmp(chunk.constData(), range.bytes.constData() + obtained, size_t(chunk.size())) != 0)
						same = false;
					obtained += chunk.size();
				}
			}
			same = same && obtained == length && file.error() == QFileDevice::NoError;
			ranges.append(QJsonObject{{QStringLiteral("offset"), range.offset}, {QStringLiteral("bytes"), length},
				{QStringLiteral("exactOriginalBytes"), same},
				{QStringLiteral("originalSha256"), QString::fromLatin1(originalRangeHash.result().toHex())},
				{QStringLiteral("imageSha256"), QString::fromLatin1(QCryptographicHash::hash(range.bytes, QCryptographicHash::Sha256).toHex())}});
			exact = exact && same;
			if (bounded)
				previousEnd = range.offset + length;
			storedBytes += length;
		}
		wellFormed = wellFormed && storedBytes == image.storedBytes() && image.acquiredBytes() >= storedBytes;
		result.insert(QStringLiteral("rangesWellFormed"), wellFormed);
		result.insert(QStringLiteral("exactOriginalRanges"), exact && wellFormed);
		result.insert(QStringLiteral("originalRangeSha256"), QString::fromLatin1(originalHash.result().toHex()));
		result.insert(QStringLiteral("imageRangeSha256"), QString::fromLatin1(imageHash.result().toHex()));
		result.insert(QStringLiteral("ranges"), ranges);
		if (file.error() != QFileDevice::NoError)
			result.insert(QStringLiteral("error"), file.errorString());
		return result;
	}

	MediaEngine::ParsedSource originalSource(const MediaEngine::SourceCandidate &candidate, const MediaEngine::StoredSource &stored,
		const MediaEngine::Cancellation &cancellation)
	{
		QFile input(candidate.path);
		if (!input.open(QIODevice::ReadOnly))
			throw std::runtime_error(QStringLiteral("Cannot verify original %1: %2").arg(candidate.path, input.errorString()).toStdString());
		const MediaEngine::ReaderContext context{stored.snapshot, cancellation};
		MediaEngine::ParsedSource result;
		switch (candidate.hint)
		{
		case MediaEngine::SourceCandidate::ReaderHint::Pmr: result = MediaEngine::PmrReader{}.read(input, context); break;
		case MediaEngine::SourceCandidate::ReaderHint::Mdb: result = MediaEngine::MdbReader{}.read(input, context); break;
		case MediaEngine::SourceCandidate::ReaderHint::Mxf: result = MediaEngine::MxfReader{}.read(input, context); break;
		case MediaEngine::SourceCandidate::ReaderHint::LegacyMedia: result = MediaEngine::OmfReader{}.read(input, context); break;
		}
		// A direct reader has no scheduling decision; the scan comparison hashes
		// actual decisions separately, while this check compares obtained records.
		result.readReason = stored.readReason;
		return result;
	}

	QByteArray interpretationDigest(const MediaEngine::ParsedSource &source, const MediaEngine::Cancellation &cancellation,
		SnapshotIds *snapshots = nullptr)
	{
		Fingerprint proof(snapshots);
		if (source.container == MediaEngine::ParsedSource::Container::Avb)
			proof.avb(source, cancellation);
		else
			proof.projection(MediaEngineProof::project(source, cancellation));
		return proof.result();
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
	parser.setApplicationDescription(QStringLiteral("Read-only MediaEngine source-retention, semantic and process-memory comparison"));
	parser.addHelpOption();
	// All modes share readers and matching; only source retention differs.
	const QCommandLineOption engineOption(QStringLiteral("engine"), QStringLiteral("Storage mode: archive, native (replay), or metadata (live source retention)"), QStringLiteral("mode"));
	const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("JSON report path; - writes stdout"), QStringLiteral("path"), QStringLiteral("-"));
	const QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("Optional app-boundary CSV path"), QStringLiteral("path"));
	const QCommandLineOption expectedRowsOption(QStringLiteral("expected-rows"), QStringLiteral("Require this physical row count"), QStringLiteral("count"));
	const QCommandLineOption noOmfOption(QStringLiteral("no-omf"), QStringLiteral("Disable OMF-family discovery"));
	const QCommandLineOption measureOnlyOption(QStringLiteral("measure-only"), QStringLiteral("Verify scan rows and receipts without restoring/reparsing sources or checking original source bytes"));
	parser.addOptions({engineOption, outputOption, csvOption, expectedRowsOption, noOmfOption, measureOnlyOption});
	parser.addPositionalArgument(QStringLiteral("roots"), QStringLiteral("One or more managed roots or direct containing bases"), QStringLiteral("roots..."));
	parser.process(app);
	const auto engine = parser.value(engineOption);
	if ((engine != QLatin1String("archive") && engine != QLatin1String("native") && engine != QLatin1String("metadata")) || parser.positionalArguments().isEmpty())
		parser.showHelp(1);
	bool validRows = true;
	const qint64 expectedRows = parser.isSet(expectedRowsOption) ? parser.value(expectedRowsOption).toLongLong(&validRows) : -1;
	if (!validRows || (parser.isSet(expectedRowsOption) && expectedRows < 0))
		parser.showHelp(1);

	const bool measureOnly = parser.isSet(measureOnlyOption);
	if (engine == QLatin1String("metadata") && !measureOnly)
	{
		std::fprintf(stderr, "Metadata-only sources have no replay graph; use --measure-only to verify all retained row evidence and receipts.\n");
		return 1;
	}
	QJsonObject report{{QStringLiteral("engine"), engine}, {QStringLiteral("schemaVersion"), 2},
		{QStringLiteral("verificationMode"), measureOnly ? QStringLiteral("scan") : QStringLiteral("full")},
		{QStringLiteral("sourceVerificationPerformed"), !measureOnly}};
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
		auto scan = QSharedPointer<MediaEngine::ScanResult>::create(engine == QLatin1String("archive")
			? MediaEngine::ScanCoordinator{}.scan(request, cancellation, observers)
			: engine == QLatin1String("native")
				? MediaEngine::ScanEngine{MediaEngine::SourceRetention::Replay}.scan(request, cancellation, observers)
				: MediaEngine::ScanEngine{}.scan(request, cancellation, observers));
		const auto scanMs = timer.elapsed();
		// These samples precede all graph restoration, original rereads, hashes,
		// file-byte comparison and adapter/CSV rows. The session stays retained.
		const auto memoryRetained = memory();
		const auto afterScan = stamps(paths);
		timer.restart();
		SnapshotIds snapshots;
		snapshots.seed(*scan);
		Fingerprint filesProof(&snapshots), sourcesProof(&snapshots), projectionsProof(&snapshots), scheduleProof(&snapshots), issuesProof(&snapshots), stateProof;
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
		// Common hashes must assign receipt IDs before optional source graph
		// inspection can discover additional embedded receipts.
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
		sourcesProof.stream << qint64(scan->sources.size());
		projectionsProof.stream << qint64(scan->sources.size());
		scheduleProof.stream << qint64(scan->candidates.size()) << qint64(scan->sources.size());
		if (scan->sources.size() != scan->candidates.size())
			throw std::runtime_error("Candidate/source count differs");
		QJsonArray sourceDigests;
		QMap<QString, qint64> reasons;
		qint64 sourceObjects = 0, sourceRelationships = 0, sourceProperties = 0, originalValueBytes = 0;
		qint64 archives = 0, databaseArchives = 0, compressedBytes = 0, serializedBytes = 0, archiveBlocks = 0;
		qint64 databaseArchiveCompressedBytes = 0, mxfArchiveCompressedBytes = 0, legacyMediaArchiveCompressedBytes = 0;
		qint64 nativeImages = 0, nativeImageBytes = 0, alternativeStores = 0, unfinishedGraphs = 0;
		qint64 nativeMxfImages = 0, nativeMxfImageBytes = 0, nativeMxfAcquiredBytes = 0, nativeMxfImageRanges = 0, nativeMxfRangeCapacityBytes = 0;
		qint64 mxfArchives = 0, legacyMediaArchives = 0, completeMxfArchiveFallbacks = 0;
		qint64 headersRead = 0, headersSkipped = 0, databasesRead = 0, originalsCompared = 0;
		for (qsizetype index = 0; index < scan->sources.size(); ++index)
		{
			const auto &stored = scan->sources[index];
			const auto &candidate = scan->candidates[index];
			const bool database = candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Pmr || candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mdb;
			++reasons[stored.readReason];
			if (database)
				databasesRead += stored.outcome != MediaEngine::ParsedSource::Outcome::NotRead;
			else if (stored.outcome == MediaEngine::ParsedSource::Outcome::NotRead)
				++headersSkipped;
			else
				++headersRead;
			if (stored.archive)
			{
				++archives;
				databaseArchives += database;
				mxfArchives += candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mxf;
				legacyMediaArchives += candidate.hint == MediaEngine::SourceCandidate::ReaderHint::LegacyMedia;
				completeMxfArchiveFallbacks += engine == QLatin1String("native")
					&& candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mxf
					&& stored.outcome == MediaEngine::ParsedSource::Outcome::Complete;
				compressedBytes += stored.archive->compressedBytes();
				if (database)
					databaseArchiveCompressedBytes += stored.archive->compressedBytes();
				else if (candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mxf)
					mxfArchiveCompressedBytes += stored.archive->compressedBytes();
				else
					legacyMediaArchiveCompressedBytes += stored.archive->compressedBytes();
				serializedBytes += stored.archive->serializedBytes();
				archiveBlocks += stored.archive->blockCount();
			}
			alternativeStores += !stored.storage.isNull();
			unfinishedGraphs += !stored.unfinishedGraph.isNull();
			QJsonObject details{{QStringLiteral("path"), candidate.path}, {QStringLiteral("hint"), int(candidate.hint)},
				{QStringLiteral("outcome"), int(stored.outcome)}, {QStringLiteral("readReason"), stored.readReason},
				{QStringLiteral("retention"), stored.retention == MediaEngine::SourceRetention::MetadataOnly
					? QStringLiteral("metadata-only") : QStringLiteral("replay")},
				{QStringLiteral("sourceBackingAvailable"), bool(stored.archive || stored.storage || stored.unfinishedGraph)}};
			if (engine == QLatin1String("metadata")
				&& (stored.retention != MediaEngine::SourceRetention::MetadataOnly
					|| stored.archive || stored.storage || stored.unfinishedGraph))
				errors.append(QStringLiteral("Metadata-only source retained replay policy or backing: %1").arg(candidate.path));
			if (const auto native = dynamic_cast<const MediaEngine::DatabaseSource *>(stored.storage.data()))
			{
				++nativeImages;
				nativeImageBytes += native->image().bytes().size();
				const auto &image = native->image();
				const auto imageProof = measureOnly ? QJsonObject{
					{QStringLiteral("capturedBytes"), qint64(image.bytes().size())},
					{QStringLiteral("expectedSize"), image.expectedSize()},
					{QStringLiteral("acquisitionOutcome"), int(image.outcome())},
					{QStringLiteral("acquisitionComplete"), image.acquisitionComplete()},
					{QStringLiteral("diagnostics"), QJsonArray::fromStringList(image.diagnostics())}}
					: checkImage(image, candidate.path);
				details.insert(QStringLiteral("databaseImage"), imageProof);
				if (!measureOnly && !imageProof.value(QStringLiteral("exactOriginalBytes")).toBool())
					errors.append(QStringLiteral("Database image differs from original: %1").arg(candidate.path));
			}
			if (const auto native = dynamic_cast<const MediaEngine::MxfSource *>(stored.storage.data()))
			{
				++nativeMxfImages;
				const auto &image = native->image();
				nativeMxfImageBytes += image.storedBytes();
				nativeMxfAcquiredBytes += image.acquiredBytes();
				nativeMxfImageRanges += image.rangeCount();
				const auto imageProof = measureOnly ? mxfImageReceipt(image) : checkMxfImage(image, candidate.path);
				nativeMxfRangeCapacityBytes += imageProof.value(QStringLiteral("rangeCapacityBytes")).toInteger();
				details.insert(QStringLiteral("mxfImage"), imageProof);
				if (!measureOnly && !imageProof.value(QStringLiteral("exactOriginalRanges")).toBool())
					errors.append(QStringLiteral("MXF image ranges differ from original: %1").arg(candidate.path));
			}
			if (!measureOnly)
			{
				QByteArray graphDigest, localGraphDigest, projectionDigest, localProjectionDigest;
				{
					const auto restored = stored.restore(cancellation);
					if (!restored)
						throw std::runtime_error(QStringLiteral("Cannot restore %1").arg(candidate.path).toStdString());
					Fingerprint graphProof(&snapshots), localProof;
					graphProof.graph(*restored);
					localProof.graph(*restored);
					graphDigest = graphProof.result();
					localGraphDigest = localProof.result();
					projectionDigest = interpretationDigest(*restored, cancellation, &snapshots);
					localProjectionDigest = interpretationDigest(*restored, cancellation);
					sourceObjects += graphProof.objects;
					sourceRelationships += graphProof.relationships;
					sourceProperties += graphProof.properties;
					originalValueBytes += graphProof.encodingBytes;
					details.insert(QStringLiteral("objects"), graphProof.objects);
					details.insert(QStringLiteral("relationships"), graphProof.relationships);
					details.insert(QStringLiteral("properties"), graphProof.properties);
					details.insert(QStringLiteral("originalValueBytes"), graphProof.encodingBytes);
					details.insert(QStringLiteral("graphSerializedBytes"), graphProof.sink.serializedBytes);
					if (restored->snapshot != stored.snapshot)
						errors.append(QStringLiteral("Restoration replaced published receipt: %1").arg(candidate.path));
				} // Release this restored graph before parsing the original source.
				if (stored.outcome != MediaEngine::ParsedSource::Outcome::NotRead)
				{
					const auto original = originalSource(candidate, stored, cancellation);
					Fingerprint proof;
					proof.graph(original);
					const auto originalGraphDigest = proof.result();
					const auto originalProjectionDigest = interpretationDigest(original, cancellation);
					const bool graphSame = originalGraphDigest == localGraphDigest;
					const bool projectionSame = originalProjectionDigest == localProjectionDigest;
					details.insert(QStringLiteral("originalGraphEqual"), graphSame);
					details.insert(QStringLiteral("originalProjectionEqual"), projectionSame);
					details.insert(QStringLiteral("originalGraphSha256"), QString::fromLatin1(originalGraphDigest));
					details.insert(QStringLiteral("originalProjectionSha256"), QString::fromLatin1(originalProjectionDigest));
					if (!graphSame || !projectionSame)
						errors.append(QStringLiteral("Restored records/projection differ from direct reader: %1").arg(candidate.path));
					++originalsCompared;
				}
				details.insert(QStringLiteral("sha256"), QString::fromLatin1(graphDigest));
				details.insert(QStringLiteral("localGraphSha256"), QString::fromLatin1(localGraphDigest));
				details.insert(QStringLiteral("projectionSha256"), QString::fromLatin1(projectionDigest));
				sourcesProof.stream << qint64(index);
				sourcesProof.bytes(graphDigest);
				projectionsProof.stream << qint64(index);
				projectionsProof.bytes(projectionDigest);
			}
			sourceDigests.append(details);
			scheduleProof.stream << qint64(index);
			scheduleProof.candidate(candidate);
			scheduleProof.storedReceipt(stored);
		}
		stateProof.texts(scan->request.roots);
		stateProof.stream << scan->request.omfScan << scan->discoveryComplete << scan->cancelled
			<< scan->parsingComplete << scan->reconciliationComplete;
		QJsonObject reasonCounts;
		for (auto reason = reasons.cbegin(); reason != reasons.cend(); ++reason)
			reasonCounts.insert(reason.key(), reason.value());
		QJsonObject hashes{{QStringLiteral("filesSha256"), QString::fromLatin1(filesProof.result())},
			{QStringLiteral("schedulingSha256"), QString::fromLatin1(scheduleProof.result())},
			{QStringLiteral("issuesSha256"), QString::fromLatin1(issuesProof.result())},
			{QStringLiteral("stateSha256"), QString::fromLatin1(stateProof.result())},
			{QStringLiteral("callbacksSha256"), QString::fromLatin1(callbacks.result())}};
		if (!measureOnly)
		{
			hashes.insert(QStringLiteral("sourceGraphsSha256"), QString::fromLatin1(sourcesProof.result()));
			hashes.insert(QStringLiteral("projectionsSha256"), QString::fromLatin1(projectionsProof.result()));
		}
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
		if ((engine == QLatin1String("native") || engine == QLatin1String("metadata")) && databaseArchives != 0)
			errors.append(QStringLiteral("MediaEngine retained %1 full database archives").arg(databaseArchives));
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
		if (!measureOnly)
		{
			report.insert(QStringLiteral("sourceObjects"), sourceObjects);
			report.insert(QStringLiteral("sourceRelationships"), sourceRelationships);
			report.insert(QStringLiteral("sourceProperties"), sourceProperties);
			report.insert(QStringLiteral("originalValueBytes"), originalValueBytes);
			report.insert(QStringLiteral("originalGraphsAndProjectionsCompared"), originalsCompared);
		}
		report.insert(QStringLiteral("snapshotIdentityCount"), snapshots.size());
		report.insert(QStringLiteral("headerReads"), headersRead);
		report.insert(QStringLiteral("headerSkips"), headersSkipped);
		report.insert(QStringLiteral("databaseReads"), databasesRead);
		report.insert(QStringLiteral("readReasonCounts"), reasonCounts);
		report.insert(QStringLiteral("storage"), QJsonObject{{QStringLiteral("archives"), archives},
			{QStringLiteral("databaseArchives"), databaseArchives}, {QStringLiteral("compressedBytes"), compressedBytes},
			{QStringLiteral("serializedBytes"), serializedBytes}, {QStringLiteral("archiveBlocks"), archiveBlocks},
			{QStringLiteral("nativeDatabaseImages"), nativeImages}, {QStringLiteral("nativeImageBytes"), nativeImageBytes},
			{QStringLiteral("nativeDatabaseImageBytes"), nativeImageBytes},
			{QStringLiteral("nativeMxfImages"), nativeMxfImages}, {QStringLiteral("nativeMxfImageBytes"), nativeMxfImageBytes},
			{QStringLiteral("nativeMxfAcquiredBytes"), nativeMxfAcquiredBytes}, {QStringLiteral("nativeMxfImageRanges"), nativeMxfImageRanges},
			{QStringLiteral("nativeMxfRangeCapacityBytes"), nativeMxfRangeCapacityBytes},
			{QStringLiteral("mxfArchives"), mxfArchives}, {QStringLiteral("legacyMediaArchives"), legacyMediaArchives},
			{QStringLiteral("databaseArchiveCompressedBytes"), databaseArchiveCompressedBytes},
			{QStringLiteral("mxfArchiveCompressedBytes"), mxfArchiveCompressedBytes},
			{QStringLiteral("legacyMediaArchiveCompressedBytes"), legacyMediaArchiveCompressedBytes},
			{QStringLiteral("completeMxfArchiveFallbacks"), completeMxfArchiveFallbacks},
			{QStringLiteral("alternativeStores"), alternativeStores}, {QStringLiteral("unfinishedGraphs"), unfinishedGraphs}});
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
			{QStringLiteral("scan"), QStringLiteral("Synchronous engine scan including discovery, readers, storage, projection, matching and selection; excludes verification and CSV adapters.")},
			{QStringLiteral("memory"), QStringLiteral("Fresh-process current resident/physical footprint or Windows private bytes and peak resident sampled immediately after scan return, retaining session; before graph restoration, original rereads and adapter rows. Peak includes process setup, preflight discovery and the scan. Unsupported counters are omitted.")},
			{QStringLiteral("setup"), QStringLiteral("Common preflight discovery enumerates input stamps before timing. Its inventory is released before memoryBefore; stamps and bounded callback sink remain. Filesystem cache is uncontrolled; no cold-cache claim.")},
			{QStringLiteral("verification"), measureOnly
				? QStringLiteral("Scan mode hashes every final row's property fields, read coverage, observations, selections and receipt aliases, scheduling receipts, stamps, issues, callbacks and completion flags. CSV and source/folder stamp checks still run. Source graph restoration, projections, original rereads and exact native database/MXF-byte verification are omitted; storage counts and native capture sizes are reported only.")
				: QStringLiteral("Full mode verifies every recursive retained graph and projection plus fresh direct-reader original comparisons for opened sources. One graph at a time. Exact native database bytes and each acquired MXF range compared directly to unchanged source files. MXF range hashes include original offsets and lengths; bytes outside those acquired ranges are not read for this byte proof. The established reader still excludes recognized picture/sound payloads. All property fields, read coverage, observations, selections, stamps, issues, callbacks and alias topology hashed; storage representation excluded from semantic hashes.")},
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
