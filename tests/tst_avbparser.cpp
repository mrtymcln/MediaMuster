// Whole Bin loading regressions. Hand-written documents exercise framing and
// proven media membership in both byte orders. Real Media Composer OMF bins
// pin exact file identities.

#include "avbbinloader.h"
#include "mobid.h"
#include "testavb.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <array>
#include <atomic>

namespace
{
	TestAvb::Document graph(bool big)
	{
		TestAvb::Document d;
		d.bigEndian = big;
		d.objects = {
			{"ABIN", TestAvb::bin(big, {2})},
			{"CMPO", TestAvb::composition(big, TestAvb::Master, "Owned clip", 4, {3}, 1)},
			{"SCLP", TestAvb::sourceClip(big)},
			{"ATTR", TestAvb::attributes(big, 5, {}, 6)},
			{"MCBR", TestAvb::binReference(big)},
			{"MCMR", TestAvb::mobReference(big)}};
		return d;
	}

	QByteArray legacy(const QByteArray &id)
	{
		return QByteArray::fromHex("060a2b340101010101010f0013000000") + id.mid(16, 8) + QByteArray::fromHex("060e2b347f7f2a80");
	}

	QByteArray audioPlugin(bool big)
	{
		TestAvb::Bytes b(big);
		b.data = TestAvb::component(big);
		b.tags(2, 8); // track group with no tracks
		b.u8(0);
		b.u32(100);
		b.u32(0);
		b.u32(0);
		b.tags(2, 6); // track effect
		b.data += QByteArray(30, '\0');
		b.tags(2, 1); // one AudioSuite plugin with no chunks
		b.u32(1);
		b.string("Plugin");
		b.data += QByteArray(16, '\0');
		b.tags(1, 1);
		b.u8(71);
		b.u32(1);
		b.u8(71);
		b.u32(2);
		b.tags(1, 8);
		b.mob(TestAvb::Other);
		b.u8(3);
		return b.data;
	}
} // namespace

class TestAvbParser : public QObject
{
	Q_OBJECT
private slots:
	void header_recognition_data();
	void header_recognition();
	void header_recognition_requires_a_readable_file();
	void missing_file_has_diagnostic();
	void media_file_ids_only_use_msml_locators_data();
	void media_file_ids_only_use_msml_locators();
	void whole_bin_excludes_unlisted_records_data();
	void whole_bin_excludes_unlisted_records();
	void source_track_links_select_number_and_kind_data();
	void source_track_links_select_number_and_kind();
	void null_locator_ids_do_not_enable_legacy_fallback();
	void malformed_locator_keeps_other_proven_file_ids();
	void binary_master_bin_variants_are_readable_data();
	void binary_master_bin_variants_are_readable();
	void mob_looking_comment_is_not_membership();
	void valid_empty_bin_is_complete_data();
	void valid_empty_bin_is_complete();
	void strict_prefixes_never_claim_complete();
	void malformed_counts_lengths_and_references_data();
	void malformed_counts_lengths_and_references();
	void file_larger_than_256_mib_preserves_identities();
	void large_object_inventory_has_no_policy_limit();
	void reference_list_above_one_million_entries();
	void terminal_source_nulls_are_ignored_data();
	void terminal_source_nulls_are_ignored();
	void unknown_class_is_incomplete();
	void unknown_identity_extension_is_incomplete();
	void dependency_lists_validate_references_data();
	void dependency_lists_validate_references();
	void unknown_component_version_is_incomplete();
	void typed_mob_field_framing_is_validated_data();
	void typed_mob_field_framing_is_validated();
	void cancelled_read_never_becomes_valid();
	void omf_identity_does_not_match_a_byte_swapped_clip_data();
	void omf_identity_does_not_match_a_byte_swapped_clip();
	void omf_bins_preserve_file_identities();
};

void TestAvbParser::header_recognition_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("recognized");
	QTest::newRow("little-endian") << TestAvb::masterBin() << true;
	QTest::newRow("big-endian") << TestAvb::masterBin({TestAvb::Master}, true) << true;
	QTest::newRow("renamed-text") << QByteArray("A text file is not an Avid bin.") << false;
	QTest::newRow("empty-file") << QByteArray{} << false;
	const auto bin = TestAvb::masterBin();
	constexpr qsizetype signatureSize = 21;
	QTest::newRow("signature-only") << bin.first(signatureSize) << true;
	QTest::newRow("damaged-body") << (bin.first(signatureSize) + QByteArray("Broken object graph")) << true;
	for (qsizetype length = 1; length < signatureSize; ++length)
		QTest::newRow(qPrintable(QStringLiteral("truncated-signature-%1").arg(length)))
			<< bin.first(length) << false;
	for (const qsizetype offset : {qsizetype{0}, qsizetype{2}, qsizetype{8}, qsizetype{12}, qsizetype{14}})
	{
		auto damaged = bin;
		damaged[offset] = '!';
		QTest::newRow(qPrintable(QStringLiteral("damaged-signature-byte-%1").arg(offset))) << damaged << false;
	}
}

void TestAvbParser::header_recognition()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, recognized);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto path = TestAvb::write(tmp.filePath("Candidate.avb"), bytes);
	const auto header = AvbBinLoader::inspectHeader(path);
	QCOMPARE(header.recognized, recognized);
	QCOMPARE(header.error.isEmpty(), recognized);
}

void TestAvbParser::header_recognition_requires_a_readable_file()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto directory = AvbBinLoader::inspectHeader(tmp.path());
	QVERIFY(!directory.recognized);
	QVERIFY(!directory.error.isEmpty());
	const auto missing = AvbBinLoader::inspectHeader(tmp.filePath("Missing.avb"));
	QVERIFY(!missing.recognized);
	QVERIFY(!missing.error.isEmpty());
	// Extension policy belongs to the picker; the probe recognizes content.
	QVERIFY(AvbBinLoader::inspectHeader(TestAvb::write(tmp.filePath("ActualBin.txt"), TestAvb::masterBin())).recognized);
}

void TestAvbParser::missing_file_has_diagnostic()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbBinLoader::load(tmp.filePath("absent.avb"));
	QVERIFY(!result.usable);
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::media_file_ids_only_use_msml_locators_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<bool>("typed");
	for (bool big : {false, true})
		for (bool typed : {false, true})
			QTest::newRow(qPrintable(QString("%1-%2").arg(big).arg(typed))) << big << typed;
}

void TestAvbParser::media_file_ids_only_use_msml_locators()
{
	QFETCH(bool, big);
	QFETCH(bool, typed);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(big); // CMPO, SCLP and MCMR must not supply filter keys.
	QVector<quint32> listed{2};
	listed.append(TestAvb::appendFileSource(d, TestAvb::mediaLocator(big, TestAvb::Source, typed, TestAvb::Other), TestAvb::Master));
	listed.append(TestAvb::appendFileSource(d, TestAvb::mediaLocator(big, TestAvb::Source, typed), TestAvb::Master));
	listed.append(TestAvb::appendFileSource(d, TestAvb::mediaLocator(big, TestAvb::Source, typed), TestAvb::Master)); // duplicate
	d.objects[0].payload = TestAvb::bin(big, listed);
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("locators.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	// This authored source clip has no established target in the bin.
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.warnings.isEmpty());
	if (typed)
	{
		QCOMPARE(result.mediaFileIds.fullIds, (QSet<QString>{MobId::format(TestAvb::Source), MobId::format(TestAvb::Other)}));
		QVERIFY(result.mediaFileIds.legacyKeys.isEmpty());
	}
	else
	{
		QVERIFY(result.mediaFileIds.fullIds.isEmpty());
		QCOMPARE(result.mediaFileIds.legacyKeys, QSet<QString>{QString::fromLatin1(TestAvb::Source.mid(16, 8).toHex())});
	}

	const auto noLocators = AvbBinLoader::load(TestAvb::write(tmp.filePath("other-records.avb"), graph(big).bytes()));
	QVERIFY(noLocators.usable && !noLocators.coverageComplete);
	QVERIFY(noLocators.mediaFileIds.isEmpty());
}

void TestAvbParser::whole_bin_excludes_unlisted_records_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("little-endian") << false;
	QTest::newRow("big-endian") << true;
}

void TestAvbParser::whole_bin_excludes_unlisted_records()
{
	QFETCH(bool, big);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document document;
	document.bigEndian = big;
	document.objects.append(TestAvb::Document::Object{"ABIN", {}});
	const auto listed = TestAvb::appendFileSource(document, TestAvb::mediaLocator(big, TestAvb::Source), TestAvb::Source);
	TestAvb::appendFileSource(document, TestAvb::mediaLocator(big, TestAvb::Other), TestAvb::Other);
	document.objects.append({"MSML", TestAvb::mediaLocator(big, TestAvb::Master)});
	document.objects[0].payload = TestAvb::bin(big, {listed});
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("listed-only.avb"), document.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY2(result.coverageComplete, qPrintable(result.warnings.join(';')));
	QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(TestAvb::Source)});
	QVERIFY(result.mediaFileIds.legacyKeys.isEmpty());
}

void TestAvbParser::source_track_links_select_number_and_kind_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("requestedTrack");
	QTest::addColumn<int>("requestedKind");
	QTest::addColumn<bool>("includeSecondAudioTrack");
	QTest::addColumn<bool>("listMaster");
	for (const bool big : {false, true})
	{
		const auto order = big ? "BE" : "LE";
		QTest::newRow(qPrintable(QStringLiteral("%1-V1").arg(order))) << big << 1 << 1 << false << false;
		QTest::newRow(qPrintable(QStringLiteral("%1-A1").arg(order))) << big << 1 << 2 << false << false;
		QTest::newRow(qPrintable(QStringLiteral("%1-A2").arg(order))) << big << 2 << 2 << false << false;
		QTest::newRow(qPrintable(QStringLiteral("%1-A1-and-A2").arg(order))) << big << 1 << 2 << true << false;
		QTest::newRow(qPrintable(QStringLiteral("%1-listed-master").arg(order))) << big << 1 << 2 << false << true;
	}
}

void TestAvbParser::source_track_links_select_number_and_kind()
{
	QFETCH(bool, big);
	QFETCH(int, requestedTrack);
	QFETCH(int, requestedKind);
	QFETCH(bool, includeSecondAudioTrack);
	QFETCH(bool, listMaster);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto audio2Id = TestAvb::Other;
	audio2Id[31] = char(quint8(audio2Id[31]) ^ 1);
	auto sequenceId = TestAvb::Master;
	sequenceId[31] = char(quint8(sequenceId[31]) ^ 1);
	TestAvb::Document document;
	document.bigEndian = big;
	document.objects = {
		{"ABIN", {}},
		{"CMPO", {}}, // Listed sequence, prepared after its source clips are appended.
		{"CMPO", TestAvb::composition(big, TestAvb::Master, "V1 A1 A2 master", 0, {4, 5, 6}, 0, true, 0, 2, {1, 1, 2})},
		{"SCLP", TestAvb::sourceClip(big, TestAvb::Source, true, 1, 1)},
		{"SCLP", TestAvb::sourceClip(big, TestAvb::Other, true, 1, 2)},
		{"SCLP", TestAvb::sourceClip(big, audio2Id, true, 1, 2)}};
	TestAvb::appendFileSource(document, TestAvb::mediaLocator(big, TestAvb::Source), TestAvb::Source);
	TestAvb::appendFileSource(document, TestAvb::mediaLocator(big, TestAvb::Other), TestAvb::Other);
	TestAvb::appendFileSource(document, TestAvb::mediaLocator(big, audio2Id), audio2Id);
	const auto requestedClip = quint32(document.objects.size() + 1);
	document.objects.append({"SCLP", TestAvb::sourceClip(big, TestAvb::Master, true, qint16(requestedTrack), qint16(requestedKind))});
	QVector<quint32> sequenceTracks{requestedClip};
	QVector<qint16> sequenceTrackNumbers{1};
	if (includeSecondAudioTrack)
	{
		sequenceTracks.append(quint32(document.objects.size() + 1));
		sequenceTrackNumbers.append(2);
		document.objects.append({"SCLP", TestAvb::sourceClip(big, TestAvb::Master, true, 2, 2)});
	}
	document.objects[1].payload = TestAvb::composition(big, sequenceId, "Listed edit", 0, sequenceTracks, 0, true, 0, 1, sequenceTrackNumbers);
	QVector<quint32> listed{2};
	if (listMaster)
		listed.append(3);
	document.objects[0].payload = TestAvb::bin(big, listed);
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("source-tracks.avb"), document.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY2(result.coverageComplete, qPrintable(result.warnings.join(';')));
	const auto expectedId = requestedKind == 1 ? TestAvb::Source : requestedTrack == 1 ? TestAvb::Other
																					   : audio2Id;
	QSet<QString> expected{MobId::format(expectedId)};
	if (includeSecondAudioTrack)
		expected.insert(MobId::format(audio2Id));
	if (listMaster)
		expected = {MobId::format(TestAvb::Source), MobId::format(TestAvb::Other), MobId::format(audio2Id)};
	QCOMPARE(result.mediaFileIds.fullIds, expected);
	QVERIFY(result.mediaFileIds.legacyKeys.isEmpty());
}

void TestAvbParser::null_locator_ids_do_not_enable_legacy_fallback()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (bool big : {false, true})
	{
		TestAvb::Document d;
		d.bigEndian = big;
		d.objects.append(TestAvb::Document::Object{"ABIN", {}});
		const auto modern = TestAvb::appendFileSource(d, TestAvb::mediaLocator(big, TestAvb::Source, true, QByteArray(32, '\0')));
		const auto native = TestAvb::appendFileSource(d, TestAvb::mediaLocator(big, QByteArray(32, '\0'), false));
		d.objects[0].payload = TestAvb::bin(big, {modern, native});
		const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("null.avb"), d.bytes()));
		QVERIFY2(result.usable, qPrintable(result.error));
		QVERIFY(!result.coverageComplete); // No usable identity: keep the reason, never fall back.
		QVERIFY(result.mediaFileIds.isEmpty());
	}
}

void TestAvbParser::malformed_locator_keeps_other_proven_file_ids()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document d;
	auto bad = TestAvb::mediaLocator(false);
	bad[10] = char(0xff); // volume-name length runs beyond the object
	bad[11] = char(0x7f);
	d.objects.append(TestAvb::Document::Object{"ABIN", {}});
	const auto modern = TestAvb::appendFileSource(d, TestAvb::mediaLocator(false));
	const auto native = TestAvb::appendFileSource(d, TestAvb::mediaLocator(false, TestAvb::Source, false));
	const auto malformed = TestAvb::appendFileSource(d, bad);
	d.objects[0].payload = TestAvb::bin(false, {modern, native, malformed});
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("bad.avb"), d.bytes()));
	QVERIFY(result.usable && !result.coverageComplete);
	QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(TestAvb::Source)});
	QCOMPARE(result.mediaFileIds.legacyKeys.size(), 1); // Only the separately complete legacy locator.
	QVERIFY(!result.warnings.isEmpty());
}

void TestAvbParser::binary_master_bin_variants_are_readable_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<bool>("large");
	QTest::addColumn<bool>("first");
	for (bool big : {false, true})
		for (bool large : {false, true})
			for (bool first : {false, true})
				QTest::newRow(qPrintable(QString("%1-%2-%3").arg(big ? "BE" : "LE", large ? "u32" : "u16", first ? "BINF" : "ABIN")))
					<< big << large << first;
}

void TestAvbParser::binary_master_bin_variants_are_readable()
{
	QFETCH(bool, big);
	QFETCH(bool, large);
	QFETCH(bool, first);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document d;
	d.bigEndian = big;
	d.objects = {{first ? "BINF" : "ABIN", TestAvb::bin(big, {2}, large, first)},
				 {"CMPO", TestAvb::composition(big, TestAvb::Master, "Render master", 0, {}, 1)}};
	const QByteArray bytes = d.bytes();
	QVERIFY(!bytes.contains(TestAvb::Master.toHex()));
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("binary-only.avb"), bytes));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY2(result.coverageComplete, qPrintable(result.warnings.join(';')));
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::mob_looking_comment_is_not_membership()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document d;
	d.objects = {{"ABIN", TestAvb::bin(false, {2})},
				 {"CMPO", TestAvb::composition(false, TestAvb::Master, "Clip", 3)},
				 {"ATTR", TestAvb::attributes(false, 0, TestAvb::Other.toHex() + " " + TestAvb::Other.toHex().toUpper())}};
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("comment.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(result.coverageComplete);
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::valid_empty_bin_is_complete_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("little-endian") << false;
	QTest::newRow("big-endian") << true;
}

void TestAvbParser::valid_empty_bin_is_complete()
{
	QFETCH(bool, big);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("empty.avb"), TestAvb::masterBin({}, big)));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(result.coverageComplete);
	QVERIFY(result.mediaFileIds.isEmpty());
	QVERIFY(result.error.isEmpty());
}

void TestAvbParser::strict_prefixes_never_claim_complete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (bool big : {false, true})
	{
		TestAvb::Document d;
		d.bigEndian = big;
		d.objects = {{"ABIN", TestAvb::bin(big, {2})}, {"CMPO", TestAvb::composition(big)}};
		const auto full = d.bytes();
		const auto rootEnd = d.chunkOffsets[0] + 8 + d.objects[0].payload.size();
		for (qsizetype length = 0; length < full.size(); ++length)
		{
			const bool readableRoot = length >= rootEnd;
			if (!readableRoot)
				QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^cannot parse .*truncated\\.avb.*")));
			const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("truncated.avb"), full.left(length)));
			QCOMPARE(result.usable, readableRoot);
			QVERIFY(!result.coverageComplete);
			QVERIFY(readableRoot ? !result.warnings.isEmpty() : !result.error.isEmpty());
			QVERIFY(result.mediaFileIds.isEmpty());
		}
	}
}

void TestAvbParser::malformed_counts_lengths_and_references_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("readable");
	for (bool big : {false, true})
	{
		auto d = graph(big);
		const auto full = d.bytes();
		const auto add = [big](const char *name, const QByteArray &bytes, bool readable = true)
		{
			QTest::newRow(qPrintable(QString("%1-%2").arg(big ? "BE" : "LE", name))) << bytes << readable;
		};
		auto broken = full;
		TestAvb::replaceU32(broken, d.countOffset, 0xffffffff, big);
		add("object-count-overflow", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.countOffset, quint32(d.objects.size() - 1), big);
		add("undeclared-trailing-object", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 0, big);
		add("null-root", broken, false);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 7, big);
		add("root-out-of-range", broken, false);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 2, big);
		add("root-is-not-bin", broken, false);
		broken = full;
		TestAvb::replaceU32(broken, d.chunkOffsets[1] + 4, 0xffffffff, big);
		add("chunk-size-overflow", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.chunkOffsets[0] + 8 + 16, 7, big);
		add("bin-item-reference-out-of-range", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.chunkOffsets[0] + 8 + 16, 3, big);
		add("bin-item-is-not-composition", broken);
		d.objects[1].payload = TestAvb::composition(big, TestAvb::Master, "bad track", 0, {7});
		add("track-reference-out-of-range", d.bytes());
		d = graph(big);
		d.objects[3].payload = TestAvb::attributes(big, 7);
		add("attribute-reference-out-of-range", d.bytes());
		d = graph(big);
		d.objects[0].payload = TestAvb::bin(big, {}, true);
		TestAvb::replaceU32(d.objects[0].payload, 14, 0xffffffff, big);
		add("large-bin-item-count-overflow", d.bytes());
	}
}

void TestAvbParser::malformed_counts_lengths_and_references()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, readable);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("broken.avb"), bytes));
	QCOMPARE(result.usable, readable);
	QVERIFY(!result.coverageComplete);
	QVERIFY(readable ? !result.warnings.isEmpty() : !result.error.isEmpty());
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::file_larger_than_256_mib_preserves_identities()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document document;
	document.objects = {{"ABIN", {}}, {"ZZZZ", QByteArray(1, '\x03')}};
	const auto listed = TestAvb::appendFileSource(document, TestAvb::mediaLocator(false));
	document.objects[0].payload = TestAvb::bin(false, {listed});
	auto bytes = document.bytes();
	constexpr quint32 payloadSize = 256 * 1024 * 1024 + 1;
	TestAvb::replaceU32(bytes, document.chunkOffsets[1] + 4, payloadSize);
	const auto payloadStart = document.chunkOffsets[1] + 8;
	QFile file(tmp.filePath("large.avb"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(bytes.first(payloadStart)), payloadStart);
	// A sparse opaque payload exercises large offsets without a large allocation.
	QVERIFY(file.seek(payloadStart + payloadSize - 1));
	QCOMPARE(file.write("\x03", 1), qint64(1));
	const auto tail = bytes.sliced(document.chunkOffsets[2]);
	QCOMPARE(file.write(tail), tail.size());
	file.close();
	const auto result = AvbBinLoader::load(file.fileName());
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete); // Opaque unknown objects cannot establish complete coverage.
	QVERIFY(!result.warnings.isEmpty());
	QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(TestAvb::Source)});
}

void TestAvbParser::large_object_inventory_has_no_policy_limit()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document document;
	document.objects = {{"ABIN", {}}};
	const auto listed = TestAvb::appendFileSource(document, TestAvb::mediaLocator(false));
	document.objects[0].payload = TestAvb::bin(false, {listed});
	auto bytes = document.bytes();
	// Exercise a large object count without imposing an estimated-memory cutoff.
	constexpr quint32 extraObjects = 2'097'152;
	TestAvb::replaceU32(bytes, document.countOffset, extraObjects + quint32(document.objects.size()));
	TestAvb::Bytes object(false);
	object.fourcc("ZZZZ");
	object.u32(1);
	object.u8(3);
	QFile file(tmp.filePath("many-objects.avb"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(bytes), bytes.size());
	const auto objects = object.data.repeated(extraObjects);
	QCOMPARE(file.write(objects), objects.size());
	file.close();
	const auto result = AvbBinLoader::load(file.fileName());
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete); // Opaque unknown objects cannot establish complete coverage.
	QVERIFY(!result.warnings.isEmpty());
	QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(TestAvb::Source)});
}

void TestAvbParser::reference_list_above_one_million_entries()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	constexpr quint32 count = 1'000'001;
	TestAvb::Bytes reference(false);
	reference.u32(2);
	TestAvb::Bytes payload(false);
	payload.tags(2, 1);
	payload.u32(count);
	payload.data += reference.data.repeated(count);
	payload.u8(3);
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false, {2})},
						{"CMPO", TestAvb::composition(false)},
						{"PRLS", payload.data}};
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("many-references.avb"), document.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(result.coverageComplete);
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::terminal_source_nulls_are_ignored_data()
{
	QTest::addColumn<QByteArray>("nullId");
	QTest::addColumn<bool>("typed");
	QTest::newRow("all-zero-typed") << QByteArray(32, '\0') << true;
	QTest::newRow("legacy-wrapper-zero-core") << legacy(QByteArray(32, '\0')) << true;
	QTest::newRow("native-zero-words") << QByteArray(32, '\0') << false;
}

void TestAvbParser::terminal_source_nulls_are_ignored()
{
	QFETCH(QByteArray, nullId);
	QFETCH(bool, typed);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[2].payload = TestAvb::sourceClip(false, nullId, typed);
	d.objects[5].payload = TestAvb::mobReference(false, nullId, typed);
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("null-source.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QCOMPARE(result.coverageComplete, typed); // Legacy-only terminal meaning stays qualified.
	QVERIFY(result.mediaFileIds.isEmpty());
	d.objects[1].payload = TestAvb::composition(false, nullId, "Null master", 4, {3}, 0, typed);
	const auto nullMaster = AvbBinLoader::load(TestAvb::write(tmp.filePath("null-master.avb"), d.bytes()));
	QVERIFY2(nullMaster.usable, qPrintable(nullMaster.error));
	QCOMPARE(nullMaster.coverageComplete, typed);
	QVERIFY(nullMaster.mediaFileIds.isEmpty());
}

void TestAvbParser::unknown_class_is_incomplete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[2] = {"ZZZZ", QByteArray::fromHex("0201") + TestAvb::Source.toHex() + QByteArray(1, '\x03')};
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("unknown-dependency.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.warnings.isEmpty());
	QVERIFY(result.warnings.join(';').contains(QStringLiteral("ZZZZ")));
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::typed_mob_field_framing_is_validated_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("valid");
	QTest::addColumn<bool>("resolved");
	for (const bool big : {false, true})
	{
		const QVector<TestAvb::Document::Object> objects = {
			{"CMPO", TestAvb::composition(big)}, {"SCLP", TestAvb::sourceClip(big)}, {"MCMR", TestAvb::mobReference(big)}, {"MSML", TestAvb::mediaLocator(big)}, {"ASPI", audioPlugin(big)}};
		for (const auto &object : objects)
		{
			const auto add = [&](const char *label, const QByteArray &payload, bool valid)
			{
				TestAvb::Document d;
				d.bigEndian = big;
				d.objects = {{"ABIN", {}}, {object.type, payload}};
				quint32 listed = 2;
				if (object.type == "MSML")
				{
					listed = 3;
					d.objects.append({"CMPO", TestAvb::composition(big, TestAvb::Master, "Locator owner", 0, {}, 0, true, 4, 3)});
					d.objects.append({"MDFL", TestAvb::fileDescriptor(big, 2)});
				}
				else if (object.type == "MCMR")
				{
					listed = 4;
					d.objects.append({"ATTR", TestAvb::attributes(big, 0, {}, 2)});
					d.objects.append({"CMPO", TestAvb::composition(big, TestAvb::Master, "Marker owner", 3)});
				}
				else if (object.type != "CMPO")
				{
					listed = 3;
					d.objects.append({"CMPO", TestAvb::composition(big, TestAvb::Master, "Component owner", 0, {2})});
				}
				d.objects[0].payload = TestAvb::bin(big, {listed});
				QTest::newRow(qPrintable(QString("%1-%2-%3").arg(QString::fromLatin1(object.type), big ? "BE" : "LE", label)))
					<< d.bytes() << valid << (object.type != "SCLP" && object.type != "ASPI");
			};
			add("valid", object.payload, true);
			// Each fixture ends with a 49-byte typed ID and the object terminator.
			auto broken = object.payload;
			broken[broken.size() - 50] = '!';
			add("wrong-tag", broken, false);
			broken = object.payload;
			TestAvb::replaceU32(broken, broken.size() - 49, 0x7fffffff, big);
			add("bad-label-length", broken, false);
			broken = object.payload;
			TestAvb::replaceU32(broken, broken.size() - 13, 0x7fffffff, big);
			add("bad-material-length", broken, false);
			broken = object.payload;
			broken.chop(2); // Remove one identity byte and the terminator: typed field cannot fit.
			add("truncated-value", broken, false);
		}
	}
}

void TestAvbParser::typed_mob_field_framing_is_validated()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, valid);
	QFETCH(bool, resolved);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("typed-fields.avb"), bytes));
	QVERIFY(result.usable); // The independently framed root is still usable.
	QCOMPARE(result.coverageComplete, valid && resolved);
	if (!valid)
	{
		QVERIFY(!result.warnings.isEmpty());
		QVERIFY(result.mediaFileIds.isEmpty());
	}
}

void TestAvbParser::unknown_identity_extension_is_incomplete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	auto &payload = d.objects[1].payload;
	// Add a future extension after the known binary identity. Its meaning
	// cannot be guessed from the bytes, even though the document is framed.
	payload.insert(payload.size() - 1, QByteArray::fromHex("017f4100000000"));
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("new-extension.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.warnings.isEmpty());
}

void TestAvbParser::dependency_lists_validate_references_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("valid");
	for (const auto &type : {QByteArray("SEQU"), QByteArray("PRLS"), QByteArray("TMCS")})
		for (const bool big : {false, true})
			for (const bool valid : {false, true})
			{
				auto d = graph(big);
				const quint32 reference = valid ? 7 : 8;
				if (type == "SEQU")
				{
					d.objects[2] = {type, TestAvb::sequence(big, {reference})};
					d.objects.append({"SCLP", TestAvb::sourceClip(big)});
				}
				else
				{
					d.objects[5] = {type, TestAvb::referenceList(big, {reference}, type == "TMCS")};
					d.objects.append({"MCMR", TestAvb::mobReference(big)});
				}
				QTest::newRow(qPrintable(QString("%1-%2-%3").arg(QString::fromLatin1(type), big ? "BE" : "LE", valid ? "valid" : "bad-reference"))) << d.bytes() << valid;
			}
}

void TestAvbParser::dependency_lists_validate_references()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, valid);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("dependency.avb"), bytes));
	QVERIFY(result.usable);
	QVERIFY(!result.coverageComplete); // Missing references qualify coverage, not known independent fields.
	QVERIFY(!result.warnings.isEmpty());
	QVERIFY(result.mediaFileIds.isEmpty());
	Q_UNUSED(valid);
}

void TestAvbParser::unknown_component_version_is_incomplete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[2] = {"SEQU", TestAvb::sequence(false, {7})};
	d.objects.append({"SCLP", TestAvb::sourceClip(false)});
	d.objects[2].payload[1] = '\x7f';
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("new-component.avb"), d.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.warnings.isEmpty());
}

void TestAvbParser::cancelled_read_never_becomes_valid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	std::atomic_bool cancelled{true};
	const auto result = AvbBinLoader::load(TestAvb::write(tmp.filePath("cancelled.avb"), TestAvb::masterBin()), &cancelled);
	QVERIFY(!result.usable);
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mediaFileIds.isEmpty());
}

void TestAvbParser::omf_identity_does_not_match_a_byte_swapped_clip_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<bool>("typed");
	QTest::newRow("LE-native") << false << false;
	QTest::newRow("LE-typed") << false << true;
	QTest::newRow("BE-native") << true << false;
	QTest::newRow("BE-typed") << true << true;
}

void TestAvbParser::omf_identity_does_not_match_a_byte_swapped_clip()
{
	QFETCH(bool, big);
	QFETCH(bool, typed);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	// These are distinct OMF identities. Pin the bytes directly so the
	// expectation cannot repeat the parser's byte-order conversion mistake.
	const QByteArray own = QByteArray::fromHex("060a2b340101010101010f00130000001122334455667788060e2b347f7f2a80");
	const QString ownKey = QStringLiteral("060a2b3401010101.01010f0013000000.1122334455667788.060e2b347f7f2a80");
	const QString unrelatedKey = QStringLiteral("060a2b3401010101.01010f0013000000.4433221166558877.060e2b347f7f2a80");
	TestAvb::Document document;
	document.bigEndian = big;
	document.objects = {{"ABIN", TestAvb::bin(big, {2})},
						{"CMPO", TestAvb::composition(big, own, "OMF master", 0, {}, 0, typed, 4)},
						{"MSML", TestAvb::mediaLocator(big, own, typed)},
						{"MDFL", TestAvb::fileDescriptor(big, 3)}};
	const auto result = AvbBinLoader::load(TestAvb::write(temp.filePath("legacy.avb"), document.bytes()));
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY2(result.coverageComplete, qPrintable(result.warnings.join(';')));
	QVERIFY(result.mediaFileIds.matches(AvbFileId::fromMobId(ownKey)));
	QVERIFY(!result.mediaFileIds.matches(AvbFileId::fromMobId(unrelatedKey)));
}

void TestAvbParser::omf_bins_preserve_file_identities()
{
	struct Pin
	{
		const char *file;
		const char *fileMob;
	};
	// Whole Bin filtering keeps the exact file locators from these real bins.
	const std::array<Pin, 2> pins{{{"WAVE(OMF).avb", "060a2b340101010101010f00130000007429976a70397047060e2b347f7f2a80"},
								   {"AIFF-C(OMF).avb", "060a2b340101010101010f00130000009729976a3ec57047060e2b347f7f2a80"}}};
	for (const auto &pin : pins)
	{
		const auto result = AvbBinLoader::load(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/bins/") + QLatin1String(pin.file));
		QVERIFY2(result.usable, qPrintable(result.error));
		QVERIFY2(result.coverageComplete, qPrintable(result.warnings.join(';')));
		QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(QByteArray::fromHex(pin.fileMob))});
	}
}

QTEST_APPLESS_MAIN(TestAvbParser)
#include "tst_avbparser.moc"
