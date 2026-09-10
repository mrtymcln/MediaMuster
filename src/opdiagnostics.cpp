#include "opdiagnostics.h"
#include "oprescue.h"
#include "oprunner.h"
#include "mxfparser.h"
#include "mobid.h"
#include "rebalancer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStorageInfo>
#include <QSysInfo>
#include <QUuid>
#include <stdexcept>
#ifdef Q_OS_MAC
#include <sys/mount.h>
#endif

namespace
{
using Sync = NativeFile::SyncResult;
class Sink : public OpSink
{
  public:
	QVector<OpResult> results;
	QStringList messages;
	OpDiagnostics::Progress update;
	void progress(const QString &name, int, int, double) override
	{
		if (update)
			update(name);
	}
	void log(QtMsgType, const QString &message) override
	{
		messages.append(message);
	}
	void trashUsed(const QString &, int) override {}
	void result(const OpResult &value) override
	{
		results.append(value);
		if (!value.message.isEmpty())
			messages.append(value.name + ": " + value.message);
	}
};

void create(const QString &path, const QByteArray &bytes,
			const NativeFile::DirectorySync &sync)
{
	QString error;
	if (OpFile::makeDirectory(QFileInfo(path).absolutePath(), error, sync) == Sync::Failed)
		throw std::runtime_error(error.toStdString());
	auto file = OpFile::open(path, true, error);
	if (!file || file->io().write(bytes) != bytes.size() ||
		file->sync() == NativeFile::SyncResult::Failed)
		throw std::runtime_error(
			("Cannot prepare disposable test file: " + path + " " + error).toStdString());
}

QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject volume(const QString &path)
{
	QString mountPath = path;
#ifdef Q_OS_MAC
	// Qt 6.5 can associate firmlink paths such as /Users with the sealed
	// system volume. Ask the kernel which filesystem actually holds the folder.
	struct statfs native{};
	const bool nativeAvailable = ::statfs(QFile::encodeName(path).constData(), &native) == 0;
	if (nativeAvailable)
		mountPath = QFile::decodeName(native.f_mntonname);
#endif
	const QStorageInfo info(mountPath);
	QJsonObject result = {{"selectedFolder", path},
						  {"mount", info.rootPath()},
						  {"filesystem", QString::fromLatin1(info.fileSystemType())},
						  {"device", QString::fromLatin1(info.device())},
						  {"readOnly", info.isReadOnly()},
						  {"volumeIdentity", VolumeIdentity::capture(mountPath).toJson()}};
#ifdef Q_OS_MAC
	if (nativeAvailable)
	{
		result.insert("mount", mountPath);
		result.insert("filesystem", QString::fromLatin1(native.f_fstypename));
		result.insert("device", QFile::decodeName(native.f_mntfromname));
		result.insert("readOnly", bool(native.f_flags & MNT_RDONLY));
	}
#endif
	return result;
}

using RecordCheck = std::function<void(const QString &, const QString &, const QString &)>;

void testBundledRelatives(const QString &sourceRoot, const QString &destinationRoot,
						  const QString &reportRoot, const std::atomic<bool> &cancel,
						  const OpDiagnostics::Progress &progress, const RecordCheck &record,
						  QJsonObject &reportJson, const NativeFile::DirectorySync &sync,
						  bool sourceRelocationSupported)
{
	const QStringList names{"Bundled MXF identities", "Bundled MXF copy and readback",
							"Bundled relatives Rebalance", "Bundled relatives group conflict"};
	if (cancel.load())
	{
		for (const auto &name : names)
			record(name, "not tested", "Cancelled before bundled-media checks.");
		return;
	}
	const auto samples = OpDiagnostics::bundledSamples();
	QVector<OpStamp> originals;
	QVector<MxfMetadata> headers;
	QSet<QString> masters, fileMobs;
	QJsonArray manifest;
	QStringList identityErrors;
	bool valid = samples.size() == 3;
	for (const auto &path : samples)
	{
		QString error;
		const auto file = OpFile::open(path, false, error);
		const auto stamp = file ? file->stamp() : OpStamp{};
		if (!file)
			identityErrors.append(QFileInfo(path).fileName() + ": " + error);
		const auto header = file ? MxfParser::parseHeader(file->io()) : MxfMetadata{};
		originals.append(stamp);
		headers.append(header);
		valid = valid && stamp.valid() &&
				header.headerStatus == MxfMetadata::HeaderStatus::Complete &&
				header.hasMaterialPackage && !MobId::toPmrForm(header.umid).isEmpty() &&
				!MobId::isAllZero(header.umid) && !MobId::toPmrForm(header.fileMobId).isEmpty() &&
				!MobId::isAllZero(header.fileMobId);
		masters.insert(header.umid);
		fileMobs.insert(header.fileMobId);
		manifest.append(QJsonObject{{"name", QFileInfo(path).fileName()},
									{"path", path},
									{"bytes", stamp.size},
									{"identityError", error},
									{"masterMobId", header.umid},
									{"fileMobId", header.fileMobId}});
	}
	valid = valid && masters.size() == 1 && fileMobs.size() == 3;
	reportJson.insert("bundledSamples", manifest);
	record(names[0], valid ? "passed" : "failed",
		   valid ? "Three distinct file MobIds share one MasterMobId: " + headers[0].umid
		   : identityErrors.isEmpty()
			   ? "Bundled MXFs do not identify three readable relatives of one master clip."
			   : identityErrors.join('\n'));
	if (!valid)
	{
		for (int i = 1; i < names.size(); ++i)
			record(names[i], "not tested", "Bundled identity check failed.");
		return;
	}

	// Seed disposable copies on the selected source storage, then copy from
	// that storage to the selected destination. Packaged originals are read only.
	const QString sourceMxf = sourceRoot + "/bundled/Avid MediaFiles/MXF";
	const QString destinationMxf = destinationRoot + "/bundled/Avid MediaFiles/MXF";
	QVector<MediaFile> sourceFiles, destinationFiles;
	QHash<QString, QString> hashes;
	Sink sink;
	sink.update = progress;
	OpRunner runner(sink, cancel);
	runner.hooks.directorySync = sync;
	bool copied = true;
	for (int i = 0; i < samples.size() && !cancel.load(); ++i)
	{
		OpRequest request;
		request.destRoot = sourceMxf + "/MediaMusterTest." + QString::number(i + 1);
		OpItem item;
		item.src = samples[i];
		item.name = QFileInfo(item.src).fileName();
		item.bytes = originals[i].size;
		item.mobId = headers[i].fileMobId;
		item.masterMobId = headers[i].umid;
		request.items.append(item);
		const auto seed =
			runner.run(request, reportRoot + "/journals/bundled-seed-" + QString::number(i));
		if (seed.succeeded != 1)
		{
			copied = false;
			break;
		}
		MediaFile media;
		media.filePath = request.destRoot + '/' + item.name;
		media.fileName = item.name;
		media.mxfFolder = QFileInfo(media.filePath).dir().dirName();
		media.sizeBytes = item.bytes;
		media.masterMobId = item.masterMobId;
		media.mobId = item.mobId;
		media.modified = QFileInfo(media.filePath).lastModified();
		sourceFiles.append(media);
		request.items[0].src = media.filePath;
		request.destRoot = destinationMxf + "/MediaMusterTest." + QString::number(i + 1);
		const auto journals = reportRoot + "/journals/bundled-copy-" + QString::number(i);
		const auto copy = runner.run(request, journals);
		const auto records = OpJournal::scan(journals);
		if (copy.succeeded != 1 || records.size() != 1 || records[0].entries.size() != 1 ||
			records[0].entries[0].hash.isEmpty())
		{
			copied = false;
			break;
		}
		hashes.insert(item.name, records[0].entries[0].hash);
		media.filePath = request.destRoot + '/' + item.name;
		media.modified = QFileInfo(media.filePath).lastModified();
		destinationFiles.append(media);
	}
	for (int i = 0; i < samples.size(); ++i)
		copied = copied && originals[i].unchanged(OpFile::inspect(samples[i]));
	copied = copied && sourceFiles.size() == 3 && destinationFiles.size() == 3;
	record(names[1],
		   cancel.load() ? "not tested"
		   : copied		 ? "passed"
						 : "failed",
		   sink.messages.join('\n'));
	if (!copied || cancel.load())
	{
		for (int i = 2; i < names.size(); ++i)
			record(names[i], "not tested", "Bundled copying did not complete.");
		return;
	}

	for (int scenario = 0; scenario < 2; ++scenario)
	{
		if (scenario == 0 && !sourceRelocationSupported)
		{
			record(names[2], "unsupported",
				   "The source storage does not support directory flush. Relatives stay in place; "
				   "their verified-copy checks ran separately.");
			continue;
		}
		const auto &files = scenario == 0 ? sourceFiles : destinationFiles;
		const auto &root = scenario == 0 ? sourceMxf : destinationMxf;
		const auto plan = Rebalancer::computePlan(root, "Disposable bundled relatives", files);
		const auto request = Rebalancer::requestForPlan(plan);
		bool passed = plan.ops.size() == 2 && request.items.size() == 2;
		if (passed)
			passed = !request.items[0].groupKey.isEmpty() &&
					 request.items[0].groupKey == request.items[1].groupKey &&
					 plan.ops[0].dest == plan.ops[1].dest;
		QString detail;
		if (cancel.load())
		{
			record(names[scenario + 2], "not tested", "Cancelled before this check.");
			continue;
		}
		if (!passed)
		{
			record(names[scenario + 2], "failed",
				   "Planner did not consolidate the three relatives into one folder and one "
				   "operation group.");
			continue;
		}
		QHash<QString, OpStamp> before;
		for (const auto &file : files)
			before.insert(file.filePath, OpFile::inspect(file.filePath));
		QString blocker;
		if (scenario == 1)
		{
			blocker = request.items[0].renameDst;
			create(blocker, "existing unrelated media", sync);
		}
		sink.messages.clear();
		const auto totals = runner.run(request, reportRoot + "/journals/bundled-rebalance-" +
													QString::number(scenario));
		passed = scenario == 0 ? totals.succeeded == 2 : totals.skipped == 2;
		for (const auto &file : files)
		{
			const auto path = scenario == 0
								  ? root + '/' + plan.ops[0].dest.display() + '/' + file.fileName
								  : file.filePath;
			const auto stamp = OpFile::inspect(path);
			QString error;
			auto opened = OpFile::open(path, false, error);
			const auto hash = opened ? OpCopier::hash(*opened, cancel) : OpCopier::Result{};
			passed = passed && before[file.filePath].sameObject(stamp) &&
					 hash.outcome == OpCopier::Outcome::Succeeded &&
					 hash.hash == hashes[file.fileName];
			if (scenario == 0 && path != file.filePath)
				passed = passed && !OpFile::occupied(file.filePath);
		}
		if (scenario == 1)
			passed = passed && read(blocker) == "existing unrelated media";
		detail = scenario == 0 ? "All three relatives must end in " + root + '/' +
									 plan.ops[0].dest.display() + ".\n"
							   : "One occupied destination must skip the whole moving group and "
								 "preserve every file.\n";
		record(names[scenario + 2],
			   cancel.load() ? "not tested"
			   : passed		 ? "passed"
							 : "failed",
			   detail + sink.messages.join('\n'));
	}
}
} // namespace

QStringList OpDiagnostics::bundledSamples()
{
	QDir folder(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MAC
	if (folder.dirName() == "MacOS")
		folder.setPath(QDir::cleanPath(folder.filePath("../Resources")));
#endif
	const QDir samples(folder.filePath("file-operation-samples"));
	return {samples.filePath("A01.E696869F_1DBEC1DBECCDFA.mxf"),
			samples.filePath("A02.E69686A0_1DBEC1DBECCE8A.mxf"),
			samples.filePath("V01.E696869E_1DBEC1DBECCC4V.mxf")};
}

OpDiagnostics::Report OpDiagnostics::run(const Options &options, const std::atomic<bool> &cancel,
										 const Progress &progress)
{
	Report report;
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString sourceRoot =
		OpJournal::canonicalPath(QDir(options.sourceArea).filePath("MediaMuster_Test_" + id));
	const QString destinationRoot = OpJournal::canonicalPath(
		QDir(options.destinationArea).filePath("MediaMuster_Test_Destination_" + id));
	const QString reportRoot = OpJournal::canonicalPath(QDir(options.reportArea).filePath(id));
	report.path = reportRoot + "/report.json";
	report.json = {{"schema", 3},
				   {"started", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
				   {"os", QSysInfo::prettyProductName()},
				   {"kernel", QSysInfo::kernelVersion()},
				   {"architecture", QSysInfo::currentCpuArchitecture()},
				   {"qt", qVersion()},
				   {"source", volume(options.sourceArea)},
				   {"destination", volume(options.destinationArea)},
				   {"storageNotes", options.storageNotes},
				   {"sourceTestFolder", sourceRoot},
				   {"destinationTestFolder", destinationRoot}};
	QJsonArray checks;
	report.text = "MediaMuster file operation test\n" + QSysInfo::prettyProductName() + " / Qt " +
				  qVersion() + "\nSource test folder: " + sourceRoot +
				  "\nDestination test folder: " + destinationRoot + "\n\n";
	auto record = [&](const QString &name, const QString &status, const QString &detail)
	{
		checks.append(QJsonObject{{"name", name}, {"status", status}, {"detail", detail}});
		const auto line =
			status.toUpper() + " — " + name + (detail.isEmpty() ? QString() : "\n" + detail);
		report.text += line + "\n\n";
		if (progress)
			progress(line);
	};
	// Different nonzero bytes catch truncation and offset mistakes.
	// Eight MiB spans two chunks; real media has separate checks below.
	QByteArray payload(8 * 1024 * 1024, Qt::Uninitialized);
	for (qsizetype n = 0; n < payload.size(); ++n)
		payload[n] = char((n * 31 + n / 4093) % 251);
	try
	{
		QString error;
		// Prepare the local report folder first so storage setup failures
		// can still be saved automatically for diagnosis.
		if (OpFile::makeDirectory(reportRoot, error) != Sync::Ok ||
			OpFile::makeDirectory(sourceRoot, error, options.directorySync) == Sync::Failed ||
			OpFile::makeDirectory(destinationRoot, error, options.directorySync) == Sync::Failed)
			throw std::runtime_error(error.toStdString());
		auto directoryCheck = [&](const QString &name, const QString &path)
		{
			QString detail;
			const auto status = options.directorySync(path, &detail);
			record(name, status == Sync::Ok ? "passed" :
				   status == Sync::OkDegraded ? "unsupported" : "failed",
				   status == Sync::Ok ? "Directory flush acknowledged for " + path :
				   detail + "\nVerified copies retain their originals. Trash and Rebalance require "
							"directory persistence support.");
			if (status == Sync::Failed)
				throw std::runtime_error(detail.toStdString());
			return status == Sync::Ok;
		};
		const bool sourceRelocationSupported = directoryCheck("Source directory persistence", sourceRoot);
		directoryCheck("Destination directory persistence", destinationRoot);

		const QStringList scenarios{"Copy and readback",
									"Keep Both with a late conflict",
									"Skip",
									"Cancel during copy",
									"Journal failure",
									"Interrupted copy and repeated recovery",
									"Move",
									"MediaMuster Trash",
									"Rebalance relocation",
									"Rebalance group conflict"};
		for (int index = 0; index < scenarios.size(); ++index)
		{
			const auto name = scenarios[index];
			if (cancel.load())
			{
				record(name, "not tested", "Cancelled before this check.");
				continue;
			}
			if ((index == 7 || index == 8) && !sourceRelocationSupported)
			{
				record(name, "unsupported",
					   "The source storage does not support directory flush. Existing files stay "
					   "in place; this check requires native relocation.");
				continue;
			}
			if (progress)
				progress(name);
			const QString source = sourceRoot + '/' + QString::number(index) + "/clip.bin";
			QString destination = destinationRoot + '/' + QString::number(index);
			if (index >= 7)
				destination = sourceRoot + "/relocations/" + QString::number(index);
			const QString journals = reportRoot + "/journals/" + QString::number(index);
			create(source, payload, options.directorySync);
			if (OpFile::makeDirectory(destination, error, options.directorySync) == Sync::Failed)
				throw std::runtime_error(error.toStdString());
			const auto original = OpFile::inspect(source);
			OpRequest request;
			request.destRoot = destination;
			request.diagnosticTrashRoot = sourceRoot + "/_MediaMuster_Trash";
			OpItem item;
			item.src = source;
			item.name = "clip.bin";
			item.bytes = payload.size();
			request.items.append(item);
			Sink sink;
			sink.update = progress;
			std::atomic<bool> stop{false};
			OpRunner runner(sink, stop);
			runner.hooks.directorySync = options.directorySync;
			bool raced = false, rejectJournal = false;
			runner.hooks.checkpoint = [&](const QString &stage, const OpJournal::Entry &entry)
			{
				if (cancel.load())
					stop = true;
				if (index == 1 && stage == "publishing" && !raced)
				{
					create(entry.dst, "late writer", options.directorySync);
					raced = true;
				}
				if (index == 3 && stage == "copy-chunk")
					stop = true;
				if (index == 4 && stage == "before-readback")
					rejectJournal = true;
				if (index == 5 && stage == "before-readback")
					throw std::runtime_error("Diagnostic interruption at checkpoint");
			};
			runner.hooks.fail = [&](const QString &point)
			{ return rejectJournal && point == "journal"; };
			if (index == 1 || index == 2)
			{
				create(destination + "/clip.bin", "existing file", options.directorySync);
				request.items[0].policy = index == 1 ? "keepboth" : "skip";
			}
			if (index == 6)
				request.kind = OpKind::Move;
			if (index == 7)
				request.kind = OpKind::Delete;
			if (index >= 8)
			{
				request.kind = OpKind::Rename;
				request.items[0].renameDst = destination + "/clip.bin";
				request.items[0].groupKey = "disposable relatives";
				auto audio = request.items[0];
				audio.name = "audio.bin";
				audio.src = QFileInfo(source).absolutePath() + "/audio.bin";
				audio.renameDst = destination + "/audio.bin";
				create(audio.src, payload, options.directorySync);
				request.items.append(audio);
				if (index == 9)
					create(audio.renameDst, "existing audio", options.directorySync);
			}
			const auto totals = runner.run(request, journals);
			bool passed = false;
			if (index == 0)
				passed = totals.succeeded == 1 && read(destination + "/clip.bin") == payload &&
						 read(source) == payload;
			if (index == 1)
				passed = totals.succeeded == 1 &&
						 read(destination + "/clip.bin") == "existing file" &&
						 read(destination + "/clip (2).bin") == "late writer" &&
						 read(destination + "/clip (3).bin") == payload;
			if (index == 2)
				passed = totals.skipped == 1 &&
						 read(destination + "/clip.bin") == "existing file" &&
						 read(source) == payload;
			if (index == 3)
				passed = totals.cancelled && read(source) == payload &&
						 !OpFile::occupied(destination + "/clip.bin");
			if (index == 4)
				passed = totals.needsAttention > 0 && read(source) == payload &&
						 !OpFile::occupied(destination + "/clip.bin");
			if (index == 5)
			{
				const auto first = OpRescue::run(journals);
				const auto second = OpRescue::run(journals);
				passed = !first.resumable.isEmpty() && !second.resumable.isEmpty() &&
						 read(source) == payload && !OpFile::occupied(destination + "/clip.bin") &&
						 OpJournal::scan(journals).size() == 1;
				if (first.resumable.isEmpty())
					error = first.notes.join('\n');
			}
			if (index == 6)
				passed = (totals.succeeded == 1 || totals.retained == 1) &&
						 read(destination + "/clip.bin") == payload &&
						 (totals.retained == 0 || read(source) == payload);
			if (index == 7)
				passed = totals.succeeded == 1 && !sink.results.isEmpty() &&
						 original.sameObject(OpFile::inspect(sink.results.last().destination)) &&
						 !OpFile::occupied(source);
			if (index == 8)
				passed = totals.succeeded == 2 && !OpFile::occupied(source) &&
						 read(destination + "/clip.bin") == payload &&
						 read(destination + "/audio.bin") == payload;
			if (index == 9)
				passed = totals.skipped == 2 && read(source) == payload &&
						 read(request.items[1].src) == payload &&
						 read(destination + "/audio.bin") == "existing audio";
			QString detail = sink.messages.join('\n');
			if (index == 4)
				detail.prepend("A journal failure is deliberately injected in this check.\n");
			if (index == 5)
				detail.prepend("An interruption is deliberately injected before verification.\n");
			if (index == 5 && !error.isEmpty())
				detail += '\n' + error;
			QString status = passed ? "passed" : "failed";
			if (cancel.load())
				status = "not tested";
			// Refusing a relocation or recovery without the necessary storage
			// contract is an unsupported configuration, never a safety pass.
			if (!passed && !cancel.load() && index == 5 && read(source) == payload &&
				detail.contains("Cannot establish the recorded volume"))
				status = "unsupported";
			record(name, status, detail);
		}

		testBundledRelatives(sourceRoot, destinationRoot, reportRoot, cancel, progress, record,
							 report.json, options.directorySync, sourceRelocationSupported);

		for (int index = 0; index < options.samples.size(); ++index)
		{
			const auto source = OpJournal::canonicalPath(options.samples[index]);
			if (cancel.load())
			{
				record("MXF sample", "not tested", source);
				continue;
			}
			const auto before = OpFile::inspect(source);
			const auto header = MxfParser::parseHeader(source);
			OpRequest request;
			request.destRoot = destinationRoot + "/samples/" + QString::number(index);
			OpItem item;
			item.src = source;
			item.name = QFileInfo(source).fileName();
			item.bytes = before.size;
			item.mobId = header.fileMobId;
			item.masterMobId = header.hasMaterialPackage ? header.umid : QString();
			request.items.append(item);
			Sink sink;
			sink.update = progress;
			OpRunner runner(sink, cancel);
			runner.hooks.directorySync = options.directorySync;
			const auto totals =
				runner.run(request, reportRoot + "/journals/sample-" + QString::number(index));
			const bool passed = totals.succeeded == 1 && before.unchanged(OpFile::inspect(source));
			record("MXF sample: " + item.name,
				   cancel.load() ? "not tested"
				   : passed		 ? "passed"
								 : "failed",
				   sink.messages.join('\n'));
		}
	}
	catch (const std::exception &exception)
	{
		record("Test setup or storage access", "failed", QString::fromUtf8(exception.what()));
	}
	record("Network disconnect, power loss and competing client", "not tested",
		   "These require separate controlled field tests. A successful local run does not qualify "
		   "another OS, client or storage configuration.");
	record("Cross-filesystem source removal", "unsupported",
		   "This phase keeps the original after a verified copy. Same-filesystem Move and Trash "
		   "use native relocation.");
	report.text += "Test files and journals are retained for inspection. Selected sample originals "
				   "were only read.\nReport: " +
				   report.path + '\n';
	report.json.insert("checks", checks);
	report.json.insert("cancelled", cancel.load());
	report.json.insert("summary", report.text);
	QSaveFile file(report.path);
	const auto data = QJsonDocument(report.json).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
		report.text += "The report could not be saved automatically. Use Save Report to choose "
					   "another location.\n";
	return report;
}
