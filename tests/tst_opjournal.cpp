#include "opjournal.h"
#include "oprequest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

namespace
{
OpRequest requestFor(const QString &root)
{
	OpRequest request;
	request.kind = OpKind::Move;
	request.copyThenRemove = true;
	request.destRoot = root + "/destination";
	OpItem item;
	item.src = root + "/source.bin";
	item.name = "source.bin";
	item.bytes = 7;
	request.items.append(item);
	return request;
}
bool writeBytes(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readBytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool setJournalTimes(const QString &path, const QDateTime &started, const QDateTime &modified)
{
	const auto bytes = readBytes(path);
	const auto firstLine = bytes.indexOf('\n');
	if (firstLine < 0)
		return false;
	auto begin = QJsonDocument::fromJson(bytes.left(firstLine)).object();
	begin["started"] = started.toString(Qt::ISODateWithMs);
	if (!writeBytes(path, QJsonDocument(begin).toJson(QJsonDocument::Compact) + bytes.mid(firstLine)))
		return false;
	QFile file(path);
	return file.open(QIODevice::ReadWrite) && file.setFileTime(modified, QFileDevice::FileModificationTime);
}
QString finishedJournal(const OpRequest &request, const QString &directory, QString &error)
{
	OpJournal journal;
	if (!journal.create(request, directory, error))
		return {};
	auto entry = journal.record().entries[0];
	entry.step = OpJournal::Step::Done;
	if (!journal.save(entry) || !journal.finish(false))
	{
		error = journal.error();
		return {};
	}
	return journal.path();
}
OpJournal::Entry cleanupEntry(const QString &root)
{
	OpJournal::Entry entry;
	entry.id = 0;
	entry.item.src = root + "/source.bin";
	entry.source = {"original", "volume", 7, 123};
	entry.mechanism = "copy";
	entry.retirement = root + "/.mediamuster-retire-0fdb69f1-e913-4f8c-aa9c-dcab911f4977/payload.retired";
	const auto directory = root + "/.mediamuster-8821d1f0-f06b-4b0b-85f9-c66d155d4a08";
	entry.cleanup.append({directory, {"directory", "volume", 0, 123},
						  directory + "/payload.partial", {"partial", "volume", 4, 456}, true});
	return entry;
}
} // namespace

class TestOpJournal : public QObject
{
	Q_OBJECT
  private slots:
	void serialized_kind_names_round_trip();
	void unknown_serialized_kind_name_is_refused();
	void serialized_policy_names_round_trip();
	void unknown_serialized_policy_name_is_refused();
	void saved_policy_and_inverse_identity_survive_restart();
	void incomplete_moves_are_not_completed_by_source_retention();
	void no_effect_requires_identity_evidence();
	void missing_required_policy_is_rejected();
	void missing_required_entry_evidence_is_rejected();
	void cleanup_evidence_round_trips_and_old_journals_remain_readable();
	void invalid_cleanup_evidence_is_rejected_data();
	void invalid_cleanup_evidence_is_rejected();
	void restoration_states_preserve_intent_and_original_identity();
	void restoration_resolves_source_without_destination();
	void abandoned_unresolved_job_keeps_every_record_and_artifact();
	void disconnected_storage_does_not_hide_interrupted_job();
	void inverse_creation_claims_forward_even_before_claim_append();
	void durable_claim_cannot_be_replaced();
	void interrupted_source_removal_remains_undo_candidate();
	void restored_original_keeps_completed_copy_undoable();
	void undo_does_not_expose_an_older_job();
	void maintenance_alone_does_not_offer_undo();
	void relocated_source_identity_rebinds_at_destination();
	void resolved_request_uses_the_same_paths_as_its_entries();
	void interrupted_system_trash_requires_a_saved_receipt_for_undo();
	void network_matching_requires_endpoint_and_directory_identity();
	void pruning_uses_last_update_data();
	void pruning_uses_last_update();
	void pruning_preserves_recovery_evidence_data();
	void pruning_preserves_recovery_evidence();
	void pruning_keeps_the_latest_undo_candidate_with_undo_disabled();
	void pruning_keeps_the_completed_undo_barrier();
	void pruning_keeps_linked_history_data();
	void pruning_keeps_linked_history();
	void pruning_preserves_missing_links_data();
	void pruning_preserves_missing_links();
	void pruning_respects_the_operation_lock();
	void pruning_leaves_media_and_unrelated_files_untouched();
};

// Journal enum names are persisted on disk; unknown spellings must be refused.

void TestOpJournal::serialized_kind_names_round_trip()
{
	const OpKind kinds[] = {OpKind::Copy, OpKind::Move, OpKind::Delete, OpKind::Rename,
							OpKind::Undo};
	for (const OpKind k : kinds)
	{
		const auto back = opKindFromName(opKindName(k));
		QVERIFY(back.has_value());
		QCOMPARE(*back, k);
	}
	// The exact spellings are load-bearing (they live in journal files).
	QCOMPARE(opKindName(OpKind::Copy), QStringLiteral("copy"));
	QCOMPARE(opKindName(OpKind::Move), QStringLiteral("move"));
	QCOMPARE(opKindName(OpKind::Delete), QStringLiteral("delete"));
	QCOMPARE(opKindName(OpKind::Rename), QStringLiteral("rename"));
	QCOMPARE(opKindName(OpKind::Undo), QStringLiteral("undo"));
}

void TestOpJournal::unknown_serialized_kind_name_is_refused()
{
	QVERIFY(!opKindFromName(QStringLiteral("bogus")).has_value());
	QVERIFY(!opKindFromName(QString()).has_value());
	// Case matters: the writer always emits lowercase, so anything else
	// is not one of ours.
	QVERIFY(!opKindFromName(QStringLiteral("Copy")).has_value());
}

void TestOpJournal::serialized_policy_names_round_trip()
{
	const ConflictPolicy policies[] = {ConflictPolicy::KeepBoth, ConflictPolicy::Skip};
	for (const ConflictPolicy p : policies)
	{
		const auto back = conflictPolicyFromName(conflictPolicyName(p));
		QVERIFY(back.has_value());
		QCOMPARE(*back, p);
	}
	QCOMPARE(conflictPolicyName(ConflictPolicy::KeepBoth), QStringLiteral("keepboth"));
	QCOMPARE(conflictPolicyName(ConflictPolicy::Skip), QStringLiteral("skip"));
	QVERIFY(!conflictPolicyFromName(QStringLiteral("replace")));
}

void TestOpJournal::unknown_serialized_policy_name_is_refused()
{
	QVERIFY(!conflictPolicyFromName(QStringLiteral("bogus")).has_value());
	QVERIFY(!conflictPolicyFromName(QString()).has_value());
}

void TestOpJournal::saved_policy_and_inverse_identity_survive_restart()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto request = requestFor(temp.path());
	request.kind = OpKind::Undo;
	request.undoOf = temp.path() + "/operation-forward.jsonl";
	request.verifyCopies = true;
	request.undoEnabled = true; // Runtime authorization is not a persisted preference.
	auto &item = request.items[0];
	item.expectedFileId = "selected-object";
	item.expectedVolumeId = "selected-volume";
	item.expectedModified = 123456;
	item.undoEntryId = 7;
	item.undoAction = "restoreMove";
	item.trashReceipt = "opaque-receipt";
	QVERIFY(writeBytes(item.src, "another"));
	QString path, error;
	{
		OpJournal journal;
		QVERIFY2(journal.create(request, temp.path() + "/journals", error), qPrintable(error));
		path = journal.path();
		auto entry = journal.record().entries[0];
		entry.attempts = 3;
		QVERIFY(journal.save(entry));
		QVERIFY(journal.markCopiesComplete());
	}
	const auto saved = OpJournal::readOne(path);
	QVERIFY(saved);
	QVERIFY(saved->request.verifyCopies);
	QVERIFY(saved->request.copyThenRemove);
	QVERIFY(!saved->request.undoEnabled);
	QCOMPARE(saved->request.undoOf, request.undoOf);
	QVERIFY(saved->copiesComplete);
	const auto &entry = saved->entries[0];
	QCOMPARE(entry.source.fileId, item.expectedFileId);
	QCOMPARE(entry.source.volumeId, item.expectedVolumeId);
	QCOMPARE(entry.source.modified, item.expectedModified);
	QCOMPARE(entry.source.size, item.bytes);
	QCOMPARE(entry.undoEntryId, 7);
	QCOMPARE(entry.undoAction, QString("restoreMove"));
	QCOMPARE(entry.trashReceipt, QString("opaque-receipt"));
	QCOMPARE(entry.item.expectedFileId, item.expectedFileId);
	QCOMPARE(entry.attempts, 3);
}

void TestOpJournal::incomplete_moves_are_not_completed_by_source_retention()
{
	OpJournal::Entry entry;
	for (const auto step : {OpJournal::Step::Published, OpJournal::Step::SourceRetained,
						   OpJournal::Step::RemovingSource, OpJournal::Step::CopyReady})
	{
		entry.step = step;
		QVERIFY(!entry.complete());
	}
	for (const auto step : {OpJournal::Step::SourceRemoved, OpJournal::Step::Done,
		OpJournal::Step::Skipped, OpJournal::Step::NoEffect})
	{
		entry.step = step;
		QVERIFY(entry.complete());
	}
}

void TestOpJournal::no_effect_requires_identity_evidence()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto request = requestFor(temp.path());
	QVERIFY(writeBytes(request.items[0].src, "source"));
	QString error;
	OpJournal journal;
	QVERIFY(journal.create(request, temp.path() + "/journals", error));
	auto entry = journal.record().entries[0];
	entry.dst = entry.item.src;
	entry.landed = entry.source;
	entry.step = OpJournal::Step::NoEffect;
	QVERIFY(journal.save(entry));
	const auto saved = OpJournal::readOne(journal.path());
	QVERIFY(saved);
	QCOMPARE(saved->entries[0].step, OpJournal::Step::NoEffect);
	QVERIFY(!saved->entries[0].explicitSkip);
	QVERIFY(!OpJournal::latestUndoable(temp.path() + "/journals"));
	auto invalid = entry.json();
	invalid["landed"] = OpStamp{}.json();
	QVERIFY(!OpJournal::Entry::fromJson(invalid));
	invalid = entry.json();
	invalid["sourceRemoved"] = true;
	QVERIFY(!OpJournal::Entry::fromJson(invalid));
	invalid = entry.json();
	invalid["explicitSkip"] = true;
	QVERIFY(!OpJournal::Entry::fromJson(invalid));
}

void TestOpJournal::missing_required_policy_is_rejected()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	QString path, error;
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), temp.path() + "/journals", error));
		path = journal.path();
	}
	const auto bytes = readBytes(path);
	auto record = QJsonDocument::fromJson(bytes.trimmed()).object();
	QCOMPARE(record["schema"].toInt(), 3);
	record.remove("verifyCopies");
	QVERIFY(writeBytes(path, QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n'));
	QVERIFY(!OpJournal::readOne(path));
	QVERIFY(OpJournal::interrupted(temp.path() + "/journals").isEmpty());
	QVERIFY(!OpJournal::latestUndoable(temp.path() + "/journals"));
	QVERIFY(QFile::exists(path));
}

void TestOpJournal::missing_required_entry_evidence_is_rejected()
{
	OpJournal::Entry entry;
	entry.id = 0;
	entry.item.src = "/disposable/source.bin";
	entry.verificationRequested = false;
	entry.mechanism = "copy";
	const auto record = entry.json();
	QVERIFY(OpJournal::Entry::fromJson(record));
	for (const auto *field : {"mechanism", "verificationRequested", "sourceRemoved", "undoAction", "attempts"})
	{
		auto incomplete = record;
		incomplete.remove(field);
		QVERIFY(!OpJournal::Entry::fromJson(incomplete));
	}
	auto unknown = record;
	unknown["mechanism"] = "guess-by-hash";
	QVERIFY(!OpJournal::Entry::fromJson(unknown));
}

void TestOpJournal::cleanup_evidence_round_trips_and_old_journals_remain_readable()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto entry = cleanupEntry(temp.path());
	const auto parsed = OpJournal::Entry::fromJson(entry.json());
	QVERIFY(parsed);
	QCOMPARE(parsed->cleanup.size(), 1);
	QCOMPARE(parsed->cleanup[0].directory, entry.cleanup[0].directory);
	QVERIFY(parsed->cleanup[0].directoryStamp.unchanged(entry.cleanup[0].directoryStamp));
	QCOMPARE(parsed->cleanup[0].file, entry.cleanup[0].file);
	QVERIFY(parsed->cleanup[0].fileStamp.unchanged(entry.cleanup[0].fileStamp));
	QVERIFY(parsed->cleanup[0].removeFile);
	entry.cleanup[0].file.clear();
	entry.cleanup[0].fileStamp = {};
	entry.cleanup[0].removeFile = false;
	QVERIFY(OpJournal::Entry::fromJson(entry.json()));
	auto old = entry.json();
	old.remove("cleanup");
	const auto legacy = OpJournal::Entry::fromJson(old);
	QVERIFY(legacy);
	QVERIFY(legacy->cleanup.isEmpty());
}

void TestOpJournal::invalid_cleanup_evidence_is_rejected_data()
{
	QTest::addColumn<QString>("corruption");
	for (const auto *name : {"array-type", "record-type", "relative-directory", "traversal-directory",
							 "ordinary-directory", "invalid-directory-token", "missing-directory-identity",
							 "missing-directory-volume", "outside-file", "retired-original",
							 "missing-file-identity", "missing-file-volume", "remove-missing-file",
							 "duplicate-directory", "remove-type", "stamp-type", "empty-file-with-identity"})
		QTest::newRow(name) << QString::fromLatin1(name);
}

void TestOpJournal::invalid_cleanup_evidence_is_rejected()
{
	QFETCH(QString, corruption);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto entry = cleanupEntry(temp.path()).json();
	auto records = entry["cleanup"].toArray();
	auto pending = records[0].toObject();
	if (corruption == "relative-directory") pending["directory"] = ".mediamuster-relative";
	if (corruption == "traversal-directory") pending["directory"] = temp.path() + "/../" + QFileInfo(pending["directory"].toString()).fileName();
	if (corruption == "ordinary-directory") pending["directory"] = temp.path() + "/ordinary-folder";
	if (corruption == "invalid-directory-token") pending["directory"] = temp.path() + "/.mediamuster-unproven";
	if (corruption == "missing-directory-identity") pending["directoryStamp"] = OpStamp{}.json();
	if (corruption == "missing-directory-volume")
	{
		auto stamp = pending["directoryStamp"].toObject();
		stamp["volume"] = "";
		pending["directoryStamp"] = stamp;
	}
	if (corruption == "outside-file") pending["file"] = temp.path() + "/original.bin";
	if (corruption == "retired-original")
	{
		pending["directory"] = QFileInfo(entry["retirement"].toString()).absolutePath();
		pending["file"] = entry["retirement"];
	}
	if (corruption == "missing-file-identity") pending["fileStamp"] = OpStamp{}.json();
	if (corruption == "missing-file-volume")
	{
		auto stamp = pending["fileStamp"].toObject();
		stamp["volume"] = "";
		pending["fileStamp"] = stamp;
	}
	if (corruption == "remove-missing-file")
	{
		pending["file"] = "";
		pending["fileStamp"] = OpStamp{}.json();
	}
	if (corruption == "remove-type") pending["removeFile"] = "true";
	if (corruption == "stamp-type")
	{
		auto stamp = pending["directoryStamp"].toObject();
		stamp["size"] = 0;
		pending["directoryStamp"] = stamp;
	}
	if (corruption == "empty-file-with-identity")
	{
		pending["file"] = "";
		pending["removeFile"] = false;
	}
	records[0] = pending;
	if (corruption == "record-type") records[0] = "unproven";
	if (corruption == "duplicate-directory") records.append(pending);
	entry["cleanup"] = corruption == "array-type" ? QJsonValue(pending) : QJsonValue(records);
	QVERIFY(!OpJournal::Entry::fromJson(entry));
}

void TestOpJournal::restoration_states_preserve_intent_and_original_identity()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto entry = cleanupEntry(temp.path());
	entry.step = OpJournal::Step::RemovingSource;
	QVERIFY(entry.needsOriginalRestoration()); // Includes older journals with a stranded original.
	entry.step = OpJournal::Step::RestoringSource;
	auto parsed = OpJournal::Entry::fromJson(entry.json());
	QVERIFY(parsed && parsed->needsOriginalRestoration() && !parsed->complete());
	QCOMPARE(parsed->step, OpJournal::Step::RestoringSource);
	entry.step = OpJournal::Step::SourceRestored;
	parsed = OpJournal::Entry::fromJson(entry.json());
	QVERIFY(parsed && parsed->complete() && !parsed->needsOriginalRestoration());
	QVERIFY(!parsed->sourceRemoved);
	entry.sourceRemoved = true;
	QVERIFY(!OpJournal::Entry::fromJson(entry.json()));
	entry.sourceRemoved = false;
	entry.source = {};
	QVERIFY(!OpJournal::Entry::fromJson(entry.json()));
}

void TestOpJournal::restoration_resolves_source_without_destination()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto oldRoot = temp.path() + "/old-source-mount";
	const auto newRoot = temp.path() + "/new-source-mount";
	const auto destinationRoot = temp.path() + "/offline-destination";
	auto entry = cleanupEntry(oldRoot);
	entry.cleanup.clear();
	entry.step = OpJournal::Step::RestoringSource;
	entry.dst = destinationRoot + "/source.bin";
	entry.temp = destinationRoot + "/.mediamuster-old/payload.partial";
	entry.landed = {"destination", "offline-device", 7, 123};
	const auto retired = newRoot + entry.retirement.mid(oldRoot.size());
	QVERIFY(QDir().mkpath(QFileInfo(retired).absolutePath()));
	QVERIFY(writeBytes(retired, "payload"));
	entry.source = OpFile::inspect(retired);
	entry.source.volumeId = "device-before-remount";
	auto directoryStamp = OpFile::inspectDirectory(QFileInfo(retired).absolutePath());
	QVERIFY(directoryStamp.valid());
	directoryStamp.volumeId = "device-before-remount";
	directoryStamp.modified = -123; // Its contents can legitimately change.
	entry.cleanup.append({QFileInfo(entry.retirement).absolutePath(), directoryStamp, {}, {}, false});
	VolumeIdentity originalVolume;
	originalVolume.rootPath = oldRoot;
	originalVolume.uuid = "source-volume";
	originalVolume.confidence = VolumeIdentity::Confidence::High;
	VolumeIdentity destinationVolume = originalVolume;
	destinationVolume.rootPath = destinationRoot;
	destinationVolume.uuid = "destination-volume";
	VolumeIdentity mountedSource = originalVolume;
	mountedSource.rootPath = newRoot;
	OpJournal::Record record;
	record.entries = {entry};
	record.request.items = {entry.item};
	record.request.destRoot = destinationRoot;
	record.volumes = {originalVolume, destinationVolume};
	QString error;
	auto ordinaryResume = record;
	QVERIFY(!OpJournal::resolve(ordinaryResume, error, {mountedSource}));
	QVERIFY2(OpJournal::resolveRestoration(record, error, {mountedSource}), qPrintable(error));
	QCOMPARE(record.entries[0].item.src, newRoot + "/source.bin");
	QCOMPARE(record.entries[0].retirement, retired);
	QVERIFY(record.entries[0].source.unchanged(OpFile::inspect(retired)));
	QVERIFY(record.entries[0].cleanup[0].directoryStamp.sameObject(OpFile::inspectDirectory(QFileInfo(retired).absolutePath())));
	QCOMPARE(record.entries[0].cleanup[0].directoryStamp.modified, qint64(-123));
	QCOMPARE(record.entries[0].dst, entry.dst);
	QCOMPARE(record.entries[0].temp, entry.temp);
	QVERIFY(record.entries[0].landed.unchanged(entry.landed));
	QCOMPARE(record.request.destRoot, destinationRoot);
	QCOMPARE(record.volumes[1].toJson(), destinationVolume.toJson());
	QCOMPARE(record.request.items[0].src, record.entries[0].item.src);
	QVERIFY(!OpJournal::resolveRestoration(record, error, {destinationVolume}));
}

void TestOpJournal::abandoned_unresolved_job_keeps_every_record_and_artifact()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto artifact = temp.path() + "/retained-original.bin";
	QVERIFY(writeBytes(artifact, "original"));
	QString path, error;
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		path = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step::RemovingSource;
		entry.retirement = artifact;
		entry.artifacts.append(artifact);
		QVERIFY(journal.save(entry));
	}
	const auto prefix = readBytes(path);
	QCOMPARE(OpJournal::interrupted(directory).size(), 1);
	QVERIFY2(OpJournal::dismiss(path, error), qPrintable(error));
	QVERIFY(OpJournal::interrupted(directory).isEmpty());
	QVERIFY(readBytes(path).startsWith(prefix));
	QCOMPARE(readBytes(artifact), QByteArray("original"));
	const auto saved = OpJournal::readOne(path);
	QVERIFY(saved);
	QVERIFY(saved->dismissed);
	QCOMPARE(saved->entries[0].retirement, artifact);
	QCOMPARE(saved->entries[0].step, OpJournal::Step::RemovingSource);
}

void TestOpJournal::disconnected_storage_does_not_hide_interrupted_job()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	auto request = requestFor(temp.path());
	request.items[0].src = temp.path() + "/unmounted/source.bin";
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(request, directory, error));
	}
	const auto pending = OpJournal::interrupted(directory);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].entries[0].item.src, request.items[0].src);
	QVERIFY(OpJournal::dismiss(pending[0].path, error));
	QVERIFY(OpJournal::interrupted(directory).isEmpty());
}

void TestOpJournal::inverse_creation_claims_forward_even_before_claim_append()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	QString forward, inverse, error;
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		forward = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step::Published;
		QVERIFY(journal.save(entry));
	}
	QCOMPARE(OpJournal::interrupted(directory).size(), 1);
	QVERIFY(OpJournal::latestUndoable(directory));
	auto undo = requestFor(temp.path());
	undo.kind = OpKind::Undo;
	undo.undoOf = forward;
	{
		OpJournal journal;
		QVERIFY(journal.create(undo, directory, error));
		inverse = journal.path();
	}
	const auto pending = OpJournal::interrupted(directory);
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending[0].path, inverse);
	QVERIFY(!OpJournal::latestUndoable(directory));
	QVERIFY(OpJournal::dismiss(inverse, error));
	QVERIFY(OpJournal::interrupted(directory).isEmpty());
	QVERIFY(!OpJournal::latestUndoable(directory));
}

void TestOpJournal::durable_claim_cannot_be_replaced()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	QString path, error;
	const auto inverse = directory + "/operation-inverse.jsonl";
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		path = journal.path();
		QVERIFY(journal.claimUndo(inverse));
		QVERIFY(journal.claimUndo(inverse));
		QVERIFY(!journal.claimUndo(directory + "/operation-other.jsonl"));
	}
	const auto saved = OpJournal::readOne(path);
	QVERIFY(saved);
	QCOMPARE(saved->undoPath, inverse);
}

void TestOpJournal::network_matching_requires_endpoint_and_directory_identity()
{
	VolumeIdentity recorded;
	recorded.kind = "network";
	recorded.networkId = "share://server/workspace";
	recorded.rootObjectId = "123";
	recorded.confidence = VolumeIdentity::Confidence::Med;
	recorded.rootPath = "/old/mount";
	auto remounted = VolumeIdentity::fromJson(recorded.toJson());
	remounted.rootPath = "/new/mount";
	QVERIFY(recorded.matches(remounted));
	remounted.networkId = "share://another-server/workspace";
	QVERIFY(!recorded.matches(remounted));
	remounted = recorded;
	remounted.rootObjectId = "456";
	QVERIFY(!recorded.matches(remounted));
	remounted = recorded;
	remounted.rootObjectId.clear();
	QVERIFY(!recorded.matches(remounted));
}

void TestOpJournal::interrupted_source_removal_remains_undo_candidate()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	QString path, error;
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		path = journal.path();
		auto entry = journal.record().entries[0];
		entry.mechanism = "copy";
		entry.step = OpJournal::Step::RemovingSource;
		entry.landed.fileId = "published-copy";
		entry.landed.size = 7;
		QVERIFY(journal.save(entry));
	}
	QVERIFY(OpJournal::latestUndoable(directory));
	QVERIFY(OpJournal::dismiss(path, error));
	const auto candidate = OpJournal::latestUndoable(directory);
	QVERIFY(candidate);
	QCOMPARE(candidate->path, path);
}

void TestOpJournal::restored_original_keeps_completed_copy_undoable()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto request = requestFor(temp.path());
	QVERIFY(writeBytes(request.items[0].src, "payload"));
	QString path, error;
	{
		OpJournal journal;
		QVERIFY2(journal.create(request, directory, error), qPrintable(error));
		path = journal.path();
		auto entry = journal.record().entries[0];
		entry.mechanism = "copy";
		entry.retirement = temp.path() + "/.mediamuster-retire-0fdb69f1-e913-4f8c-aa9c-dcab911f4977/payload.retired";
		entry.step = OpJournal::Step::SourceRestored;
		QVERIFY(journal.save(entry));
		QVERIFY(journal.finish(true));
	}
	QVERIFY(OpJournal::interrupted(directory).isEmpty());
	const auto candidate = OpJournal::latestUndoable(directory);
	QVERIFY(candidate);
	QCOMPARE(candidate->path, path);
	QVERIFY2(OpJournal::dismiss(path, error), qPrintable(error));
	QVERIFY(OpJournal::latestUndoable(directory));
	QCOMPARE(OpJournal::latestUndoable(directory)->path, path);
}

void TestOpJournal::undo_does_not_expose_an_older_job()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	QString newest, error;
	for (int n = 0; n < 2; ++n)
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		newest = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step::Done;
		QVERIFY(journal.save(entry));
		QTest::qWait(2); // Distinct recorded start milliseconds, independent of disk speed.
	}
	auto undo = requestFor(temp.path());
	undo.kind = OpKind::Undo;
	undo.undoOf = newest;
	{
		OpJournal journal;
		QVERIFY(journal.create(undo, directory, error));
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step::Done;
		QVERIFY(journal.save(entry));
	}
	QVERIFY(!OpJournal::latestUndoable(directory));
}

void TestOpJournal::maintenance_alone_does_not_offer_undo()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	auto request = requestFor(temp.path());
	request.items[0].maintenance = true;
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(request, directory, error));
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step::Done;
		QVERIFY(journal.save(entry));
	}
	QVERIFY(!OpJournal::latestUndoable(directory));
}

void TestOpJournal::relocated_source_identity_rebinds_at_destination()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	auto request = requestFor(temp.path());
	QVERIFY(writeBytes(request.items[0].src, "payload"));
	OpJournal::Record record;
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(request, temp.path() + "/journals", error));
		record = journal.record();
	}
	auto &entry = record.entries[0];
	entry.mechanism = "relocate";
	entry.step = OpJournal::Step::Relocating;
	entry.dst = temp.path() + "/relocated.bin";
	QVERIFY(QFile::rename(entry.item.src, entry.dst));
	entry.source.volumeId = "device-number-before-remount";
	QVERIFY(OpJournal::resolve(record, error, {VolumeIdentity::capture(temp.path())}));
	QVERIFY(record.entries[0].source.unchanged(OpFile::inspect(entry.dst)));
}

void TestOpJournal::resolved_request_uses_the_same_paths_as_its_entries()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	OpJournal::Record record;
	QString error;
	{
		OpJournal journal;
		QVERIFY(journal.create(requestFor(temp.path()), directory, error));
		record = journal.record();
	}
	const auto oldRoot = temp.path() + "/old-mount";
	const auto newRoot = temp.path() + "/new-mount";
	auto oldVolume = VolumeIdentity::capture(temp.path());
	oldVolume.rootPath = oldRoot;
	auto newVolume = oldVolume;
	newVolume.rootPath = newRoot;
	record.volumes = {oldVolume};
	record.entries[0].item.src = oldRoot + "/source.bin";
	record.entries[0].originalSource = record.entries[0].item.src;
	record.request.items[0].src = record.entries[0].item.src;
	QVERIFY(OpJournal::resolve(record, error, {newVolume}));
	QCOMPARE(record.request.items[0].src, newRoot + "/source.bin");
	QCOMPARE(record.entries[0].originalSource, oldRoot + "/source.bin");
	{
		OpJournal journal;
		QVERIFY(journal.resume(record, error));
	}
	const auto read = OpJournal::readOne(record.path);
	QVERIFY(read);
	QCOMPARE(read->request.items[0].src, read->entries[0].item.src);
	QCOMPARE(read->request.items[0].src, newRoot + "/source.bin");
}

void TestOpJournal::interrupted_system_trash_requires_a_saved_receipt_for_undo()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	QString error;
	OpJournal journal;
	QVERIFY(journal.create(requestFor(temp.path()), directory, error));
	auto entry = journal.record().entries[0];
	entry.mechanism = "systemTrash";
	entry.step = OpJournal::Step::NeedsAttention;
	entry.landed.fileId = "trashed-object";
	entry.landed.size = 7;
	QVERIFY(journal.save(entry));
	QVERIFY(!OpJournal::latestUndoable(directory));
	entry.trashReceipt = "saved-provider-receipt";
	QVERIFY(journal.save(entry));
	const auto candidate = OpJournal::latestUndoable(directory);
	QVERIFY(candidate);
	QCOMPARE(candidate->path, journal.path());
}

void TestOpJournal::pruning_uses_last_update_data()
{
	QTest::addColumn<int>("secondsFromCutoff");
	QTest::addColumn<bool>("removed");
	QTest::newRow("older-than-30-days") << -1 << true;
	QTest::newRow("exactly-30-days") << 0 << false;
	QTest::newRow("just-within-30-days") << 1 << false;
	QTest::newRow("old-job-updated-yesterday") << 29 * 24 * 60 * 60 << false;
}

void TestOpJournal::pruning_uses_last_update()
{
	QFETCH(int, secondsFromCutoff);
	QFETCH(bool, removed);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	request.items[0].maintenance = true;
	QString error;
	const auto path = finishedJournal(request, directory, error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(path, now.addDays(-100), now.addDays(-30).addSecs(secondsFromCutoff)));
	const auto before = readBytes(path);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QCOMPARE(QFile::exists(path), !removed);
	if (!removed)
		QCOMPARE(readBytes(path), before);
}

void TestOpJournal::pruning_preserves_recovery_evidence_data()
{
	QTest::addColumn<int>("step");
	QTest::addColumn<QString>("evidence");
	QTest::addColumn<bool>("removed");
	using Step = OpJournal::Step;
	QTest::newRow("completed") << int(Step::Done) << QString() << true;
	QTest::newRow("completed-source-removal") << int(Step::SourceRemoved) << QString("retirement") << true;
	QTest::newRow("completed-source-restoration") << int(Step::SourceRestored) << QString("restoration") << true;
	QTest::newRow("restoration-intent") << int(Step::RestoringSource) << QString("restoration") << false;
	QTest::newRow("completed-with-pending-cleanup") << int(Step::Done) << QString("cleanup") << false;
	QTest::newRow("dismissed-with-pending-cleanup") << int(Step::Done) << QString("dismissed-cleanup") << false;
	QTest::newRow("skipped") << int(Step::Skipped) << QString() << true;
	QTest::newRow("no-effect") << int(Step::NoEffect) << QString() << true;
	QTest::newRow("planned") << int(Step::Planned) << QString() << false;
	QTest::newRow("cancelled") << int(Step::Cancelled) << QString() << false;
	QTest::newRow("failed") << int(Step::Failed) << QString() << false;
	QTest::newRow("awaiting-source-removal") << int(Step::Published) << QString() << false;
	QTest::newRow("source-retained") << int(Step::SourceRetained) << QString() << false;
	QTest::newRow("needs-attention") << int(Step::NeedsAttention) << QString() << false;
	QTest::newRow("dismissed-failure") << int(Step::Failed) << QString("dismissed") << false;
	QTest::newRow("missing-final-stop") << int(Step::Done) << QString("active") << false;
	QTest::newRow("torn-final-append") << int(Step::Done) << QString("torn") << false;
	QTest::newRow("corrupt-append") << int(Step::Done) << QString("corrupt") << false;
	QTest::newRow("retained-artifact") << int(Step::Done) << QString("artifact") << false;
	QTest::newRow("unfinished-temporary-file") << int(Step::Done) << QString("temp") << false;
}

void TestOpJournal::pruning_preserves_recovery_evidence()
{
	QFETCH(int, step);
	QFETCH(QString, evidence);
	QFETCH(bool, removed);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	request.items[0].maintenance = true;
	QVERIFY(writeBytes(request.items[0].src, "payload"));
	QString path, error;
	{
		OpJournal journal;
		QVERIFY2(journal.create(request, directory, error), qPrintable(error));
		path = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = OpJournal::Step(step);
		if (entry.step == OpJournal::Step::NoEffect)
		{
			entry.dst = entry.item.src;
			entry.landed = entry.source;
		}
		if (evidence == "retirement")
		{
			entry.retirement = temp.path() + "/removed-original.bin";
			entry.sourceRemoved = true;
		}
		if (evidence == "restoration")
		{
			entry.mechanism = "copy";
			entry.retirement = cleanupEntry(temp.path()).retirement;
		}
		if (evidence == "cleanup" || evidence == "dismissed-cleanup")
			entry.cleanup = cleanupEntry(temp.path()).cleanup;
		if (evidence == "artifact")
			entry.artifacts.append(temp.path() + "/disconnected-volume/retained.bin");
		if (evidence == "temp")
			entry.temp = temp.path() + "/disconnected-volume/partial.bin";
		QVERIFY(journal.save(entry));
		if (evidence != "active")
			QVERIFY(journal.finish(false));
	}
	if (evidence == "dismissed" || evidence == "dismissed-cleanup")
		QVERIFY2(OpJournal::dismiss(path, error), qPrintable(error));
	if (evidence == "torn" || evidence == "corrupt")
		QVERIFY(writeBytes(path, readBytes(path) + (evidence == "torn" ? QByteArray("{\"record\":") : QByteArray("invalid\n"))));
	QVERIFY(setJournalTimes(path, now.addDays(-60), now.addDays(-40)));
	const auto record = OpJournal::readOne(path);
	QVERIFY(record);
	QCOMPARE(record->torn, evidence == "torn");
	QCOMPARE(record->corrupt, evidence == "corrupt");
	const auto before = readBytes(path);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QCOMPARE(QFile::exists(path), !removed);
	if (!removed)
		QCOMPARE(readBytes(path), before);
	QCOMPARE(readBytes(request.items[0].src), QByteArray("payload"));
}

void TestOpJournal::pruning_keeps_the_latest_undo_candidate_with_undo_disabled()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	QVERIFY(!request.undoEnabled);
	QString error;
	const auto older = finishedJournal(request, directory, error);
	const auto candidate = finishedJournal(request, directory, error);
	request.items[0].maintenance = true;
	const auto maintenance = finishedJournal(request, directory, error);
	QVERIFY2(!older.isEmpty() && !candidate.isEmpty() && !maintenance.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(older, now.addDays(-70), now.addDays(-40)));
	QVERIFY(setJournalTimes(candidate, now.addDays(-60), now.addDays(-40)));
	QVERIFY(setJournalTimes(maintenance, now.addDays(-50), now.addDays(-40)));
	const auto before = OpJournal::latestUndoable(directory);
	QVERIFY(before);
	QCOMPARE(before->path, candidate);
	QVERIFY(!before->request.undoEnabled);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QVERIFY(!QFile::exists(older));
	QVERIFY(!QFile::exists(maintenance));
	QVERIFY(QFile::exists(candidate));
	const auto after = OpJournal::latestUndoable(directory);
	QVERIFY(after);
	QCOMPARE(after->path, candidate);
}

void TestOpJournal::pruning_keeps_the_completed_undo_barrier()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	QString error;
	const auto older = finishedJournal(request, directory, error);
	const auto forward = finishedJournal(request, directory, error);
	QVERIFY2(!older.isEmpty() && !forward.isEmpty(), qPrintable(error));
	request.kind = OpKind::Undo;
	request.undoOf = forward;
	const auto inverse = finishedJournal(request, directory, error);
	QVERIFY2(!inverse.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(older, now.addDays(-80), now.addDays(-1)));
	QVERIFY(setJournalTimes(forward, now.addDays(-70), now.addDays(-40)));
	QVERIFY(setJournalTimes(inverse, now.addDays(-60), now.addDays(-40)));
	QVERIFY(!OpJournal::latestUndoable(directory));
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QVERIFY(QFile::exists(older));
	QVERIFY(QFile::exists(forward));
	QVERIFY(QFile::exists(inverse));
	QVERIFY(!OpJournal::latestUndoable(directory));
}

void TestOpJournal::pruning_keeps_linked_history_data()
{
	QTest::addColumn<QString>("retainedReason");
	QTest::addColumn<bool>("claimSaved");
	QTest::newRow("recent-forward") << QString("recent-forward") << true;
	QTest::newRow("recent-inverse-before-claim-append") << QString("recent-inverse") << false;
	QTest::newRow("unfinished-forward") << QString("unfinished-forward") << true;
	QTest::newRow("unfinished-inverse") << QString("unfinished-inverse") << true;
	QTest::newRow("old-pair-superseded-by-new-job") << QString() << true;
}

void TestOpJournal::pruning_keeps_linked_history()
{
	QFETCH(QString, retainedReason);
	QFETCH(bool, claimSaved);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	QString forward, inverse, error;
	{
		OpJournal journal;
		QVERIFY2(journal.create(request, directory, error), qPrintable(error));
		forward = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = retainedReason == "unfinished-forward" ? OpJournal::Step::Failed : OpJournal::Step::Done;
		QVERIFY(journal.save(entry));
		QVERIFY(journal.finish(false));
	}
	request.kind = OpKind::Undo;
	request.undoOf = forward;
	{
		OpJournal journal;
		QVERIFY2(journal.create(request, directory, error), qPrintable(error));
		inverse = journal.path();
		auto entry = journal.record().entries[0];
		entry.step = retainedReason == "unfinished-inverse" ? OpJournal::Step::Failed : OpJournal::Step::Done;
		QVERIFY(journal.save(entry));
		QVERIFY(journal.finish(false));
	}
	if (claimSaved)
	{
		const auto record = OpJournal::readOne(forward);
		QVERIFY(record);
		OpJournal journal;
		QVERIFY2(journal.resume(*record, error), qPrintable(error));
		QVERIFY(journal.claimUndo(inverse));
	}
	QVERIFY(setJournalTimes(forward, now.addDays(-80), now.addDays(retainedReason == "recent-forward" ? -1 : -40)));
	QVERIFY(setJournalTimes(inverse, now.addDays(-70), now.addDays(retainedReason == "recent-inverse" ? -1 : -40)));
	const auto candidate = finishedJournal(requestFor(temp.path()), directory, error);
	QVERIFY2(!candidate.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(candidate, now.addDays(-60), now.addDays(-40)));
	const auto forwardBefore = readBytes(forward);
	const auto inverseBefore = readBytes(inverse);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	const bool retained = !retainedReason.isEmpty();
	QCOMPARE(QFile::exists(forward), retained);
	QCOMPARE(QFile::exists(inverse), retained);
	if (retained)
	{
		QCOMPARE(readBytes(forward), forwardBefore);
		QCOMPARE(readBytes(inverse), inverseBefore);
	}
	const auto after = OpJournal::latestUndoable(directory);
	QVERIFY(after);
	QCOMPARE(after->path, candidate);
}

void TestOpJournal::pruning_preserves_missing_links_data()
{
	QTest::addColumn<bool>("inverse");
	QTest::newRow("missing-inverse") << false;
	QTest::newRow("missing-forward") << true;
}

void TestOpJournal::pruning_preserves_missing_links()
{
	QFETCH(bool, inverse);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto missing = directory + "/operation-missing.jsonl";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	if (inverse)
	{
		request.kind = OpKind::Undo;
		request.undoOf = missing;
	}
	QString error;
	const auto path = finishedJournal(request, directory, error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	if (!inverse)
	{
		const auto record = OpJournal::readOne(path);
		QVERIFY(record);
		OpJournal journal;
		QVERIFY2(journal.resume(*record, error), qPrintable(error));
		QVERIFY(journal.claimUndo(missing));
	}
	QVERIFY(setJournalTimes(path, now.addDays(-80), now.addDays(-40)));
	const auto candidate = finishedJournal(requestFor(temp.path()), directory, error);
	QVERIFY2(!candidate.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(candidate, now.addDays(-60), now.addDays(-40)));
	const auto before = readBytes(path);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QCOMPARE(readBytes(path), before);
	QVERIFY(!QFile::exists(missing));
}

void TestOpJournal::pruning_respects_the_operation_lock()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	request.items[0].maintenance = true;
	QString error;
	const auto path = finishedJournal(request, directory, error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(path, now.addDays(-60), now.addDays(-40)));
	const auto before = readBytes(path);
	auto lock = OpJournal::acquire(directory, error);
	QVERIFY2(lock, qPrintable(error));
	QVERIFY(!OpJournal::prune(directory, error, now));
	QVERIFY(!error.isEmpty());
	QCOMPARE(readBytes(path), before);
	lock.reset();
	error.clear();
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QVERIFY(!QFile::exists(path));
}

void TestOpJournal::pruning_leaves_media_and_unrelated_files_untouched()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto directory = temp.path() + "/journals";
	const auto now = QDateTime::fromString("2026-09-14T12:00:00Z", Qt::ISODate);
	auto request = requestFor(temp.path());
	request.items[0].maintenance = true;
	request.diagnosticTrashRoot = temp.path() + "/MediaMuster_Trash";
	QVERIFY(QDir().mkpath(request.diagnosticTrashRoot));
	const auto trashed = request.diagnosticTrashRoot + "/trashed.bin";
	QVERIFY(writeBytes(request.items[0].src, "source media"));
	QVERIFY(writeBytes(trashed, "trashed media"));
	QString error;
	const auto path = finishedJournal(request, directory, error);
	QVERIFY2(!path.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(path, now.addDays(-60), now.addDays(-40)));
	const auto notes = directory + "/notes.txt";
	const auto unknown = directory + "/operation-unrelated.jsonl";
	QVERIFY(writeBytes(notes, "keep these notes"));
	QVERIFY(writeBytes(unknown, "unrelated data\n"));
	const auto outside = finishedJournal(request, temp.path() + "/other-journals", error);
	QVERIFY2(!outside.isEmpty(), qPrintable(error));
	QVERIFY(setJournalTimes(outside, now.addDays(-60), now.addDays(-40)));
	const auto outsideBefore = readBytes(outside);
#ifdef Q_OS_UNIX
	const auto link = directory + "/operation-symlink.jsonl";
	QVERIFY(QFile::link(outside, link));
	QVERIFY(QFileInfo(link).isSymLink());
#endif
	QCOMPARE(OpJournal::scan(directory).size(), 1);
	QVERIFY2(OpJournal::prune(directory, error, now), qPrintable(error));
	QVERIFY(!QFile::exists(path));
	QCOMPARE(readBytes(request.items[0].src), QByteArray("source media"));
	QCOMPARE(readBytes(trashed), QByteArray("trashed media"));
	QCOMPARE(readBytes(notes), QByteArray("keep these notes"));
	QCOMPARE(readBytes(unknown), QByteArray("unrelated data\n"));
	QCOMPARE(readBytes(outside), outsideBefore);
#ifdef Q_OS_UNIX
	QVERIFY(QFileInfo(link).isSymLink());
#endif
}

QTEST_GUILESS_MAIN(TestOpJournal)
#include "tst_opjournal.moc"
