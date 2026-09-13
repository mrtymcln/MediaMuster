#include "opjournal.h"

#include <QFile>
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
} // namespace

class TestOpJournal : public QObject
{
	Q_OBJECT
  private slots:
	void saved_policy_and_inverse_identity_survive_restart();
	void incomplete_moves_are_not_completed_by_source_retention();
	void no_effect_requires_identity_evidence();
	void missing_required_policy_is_rejected();
	void missing_new_entry_evidence_is_not_defaulted();
	void abandoned_unresolved_job_keeps_every_record_and_artifact();
	void disconnected_storage_does_not_hide_interrupted_job();
	void inverse_creation_claims_forward_even_before_claim_append();
	void durable_claim_cannot_be_replaced();
	void interrupted_source_removal_remains_undo_candidate();
	void undo_does_not_expose_an_older_job();
	void maintenance_alone_does_not_offer_undo();
	void relocated_source_identity_rebinds_at_destination();
	void resolved_request_uses_the_same_paths_as_its_entries();
	void interrupted_system_trash_requires_a_saved_receipt_for_undo();
	void network_matching_requires_endpoint_and_directory_identity();
};

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

void TestOpJournal::missing_new_entry_evidence_is_not_defaulted()
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

QTEST_GUILESS_MAIN(TestOpJournal)
#include "tst_opjournal.moc"
