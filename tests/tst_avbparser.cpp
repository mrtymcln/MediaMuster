// Structured AVB regression tests. Hand-written complete documents exercise
// typed fields and reference ownership in both byte orders. Real Media Composer
// OMF fixtures pin compatibility with the PMR/file identities already shipped.

#include "avbparser.h"
#include "mobid.h"
#include "testavb.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <array>
#include <atomic>

namespace
{
	QSet<QString> aliases(const QVector<QByteArray> &ids)
	{
		QSet<QString> result;
		for (const auto &id : ids)
		{
			const auto formatted = MobId::format(id);
			result.insert(formatted);
			result.insert(MobId::toPmrForm(formatted));
		}
		return result;
	}

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
} // namespace

class TestAvbParser : public QObject
{
	Q_OBJECT
private slots:
	void header_recognition_data();
	void header_recognition();
	void header_recognition_requires_a_readable_file();
	void missing_file_has_diagnostic();
	void binary_only_master_data();
	void binary_only_master();
	void source_and_mob_references_and_owned_metadata_data();
	void source_and_mob_references_and_owned_metadata();
	void macroman_names_are_decoded_without_utf8_extension();
	void malformed_utf8_metadata_is_invalid();
	void mob_looking_comment_is_not_membership();
	void valid_empty_bin_is_complete_data();
	void valid_empty_bin_is_complete();
	void strict_prefixes_are_invalid();
	void malformed_counts_lengths_and_references_data();
	void malformed_counts_lengths_and_references();
	void native_legacy_words_are_decoded_data();
	void native_legacy_words_are_decoded();
	void terminal_source_nulls_are_ignored_data();
	void terminal_source_nulls_are_ignored();
	void unknown_class_is_incomplete();
	void unknown_identity_extension_is_incomplete();
	void dependency_lists_validate_references_data();
	void dependency_lists_validate_references();
	void unknown_component_version_is_incomplete();
	void typed_mob_field_framing_is_validated();
	void cancelled_read_never_becomes_valid();
	void omf_bins_preserve_file_and_master_identities();
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
	const auto header = AvbParser::inspectHeader(path);
	QCOMPARE(header.recognized, recognized);
	QCOMPARE(header.error.isEmpty(), recognized);
}

void TestAvbParser::header_recognition_requires_a_readable_file()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto directory = AvbParser::inspectHeader(tmp.path());
	QVERIFY(!directory.recognized);
	QVERIFY(!directory.error.isEmpty());
	const auto missing = AvbParser::inspectHeader(tmp.filePath("Missing.avb"));
	QVERIFY(!missing.recognized);
	QVERIFY(!missing.error.isEmpty());
	// Extension policy belongs to the picker; the probe recognizes content.
	QVERIFY(AvbParser::inspectHeader(TestAvb::write(tmp.filePath("ActualBin.txt"), TestAvb::masterBin())).recognized);
}

void TestAvbParser::missing_file_has_diagnostic()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbParser::parse(tmp.filePath("absent.avb"));
	QVERIFY(!result.valid);
	QVERIFY(!result.complete);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mobIds.isEmpty());
}

void TestAvbParser::binary_only_master_data()
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

void TestAvbParser::binary_only_master()
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
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("binary-only.avb"), bytes));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY2(result.complete, qPrintable(result.warnings.join(';')));
	QCOMPARE(result.mobIds, aliases({TestAvb::Master}));
	QCOMPARE(result.mobs.size(), 1);
	QCOMPARE(result.mobs.first().mobId, MobId::format(TestAvb::Master));
	QCOMPARE(result.mobs.first().name, QStringLiteral("Render master"));
	QCOMPARE(result.mobs.first().mobType, 2);
	QCOMPARE(result.mobs.first().usageCode, 1);
	QVERIFY(result.mobs.first().originalBin.isEmpty());
}

void TestAvbParser::source_and_mob_references_and_owned_metadata_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("little-endian") << false;
	QTest::newRow("big-endian") << true;
}

void TestAvbParser::source_and_mob_references_and_owned_metadata()
{
	QFETCH(bool, big);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(big);
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("renamed-current-bin.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY2(result.complete, qPrintable(result.warnings.join(';')));
	QCOMPARE(result.mobIds, aliases({TestAvb::Master, TestAvb::Source, TestAvb::Other}));
	const auto owner = std::find_if(result.mobs.cbegin(), result.mobs.cend(), [](const AvbMob &mob)
									{ return mob.mobId == MobId::format(TestAvb::Master); });
	QVERIFY(owner != result.mobs.cend());
	QCOMPARE(owner->name, QStringLiteral("Owned clip"));
	QCOMPARE(owner->originalBin, QString::fromUtf8("東京"));
	QCOMPARE(owner->originalBinUid, QStringLiteral("1234567890abcdef"));
	QVERIFY(owner->originalBin != result.displayName);
}

void TestAvbParser::mob_looking_comment_is_not_membership()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	TestAvb::Document d;
	d.objects = {{"ABIN", TestAvb::bin(false, {2})},
				 {"CMPO", TestAvb::composition(false, TestAvb::Master, "Clip", 3)},
				 {"ATTR", TestAvb::attributes(false, 0, TestAvb::Other.toHex() + " " + TestAvb::Other.toHex().toUpper())}};
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("comment.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(result.complete);
	QCOMPARE(result.mobIds, aliases({TestAvb::Master}));
}

void TestAvbParser::macroman_names_are_decoded_without_utf8_extension()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[1].payload = TestAvb::composition(false, TestAvb::Master, QByteArray("caf\x8e"), 4, {3});
	d.objects[4].payload = TestAvb::binReference(false, QByteArray("caf\x8e originals"), {});
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("macroman.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(result.complete);
	QCOMPARE(result.mobs.size(), 1);
	QCOMPARE(result.mobs.first().name, QStringLiteral("café"));
	QCOMPARE(result.mobs.first().originalBin, QStringLiteral("café originals"));
}

void TestAvbParser::malformed_utf8_metadata_is_invalid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[4].payload = TestAvb::binReference(false, "Fallback", QByteArray::fromHex("c328"));
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("invalid-utf8.avb"), d.bytes()));
	QVERIFY(!result.valid);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mobIds.isEmpty());
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
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("empty.avb"), TestAvb::masterBin({}, big)));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(result.complete);
	QVERIFY(result.mobIds.isEmpty());
	QVERIFY(result.mobs.isEmpty());
	QVERIFY(result.error.isEmpty());
}

void TestAvbParser::strict_prefixes_are_invalid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (bool big : {false, true})
	{
		const auto full = TestAvb::masterBin({TestAvb::Master}, big);
		for (qsizetype length = 0; length < full.size(); ++length)
		{
			QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^cannot parse .*truncated\\.avb.*")));
			const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("truncated.avb"), full.left(length)));
			QVERIFY2(!result.valid, qPrintable(QString("Accepted truncated %1-byte document (BE=%2)").arg(length).arg(big)));
			QVERIFY(!result.error.isEmpty());
			QVERIFY(result.mobIds.isEmpty());
			QVERIFY(result.mobs.isEmpty());
		}
	}
}

void TestAvbParser::malformed_counts_lengths_and_references_data()
{
	QTest::addColumn<QByteArray>("bytes");
	for (bool big : {false, true})
	{
		auto d = graph(big);
		const auto full = d.bytes();
		const auto add = [big](const char *name, const QByteArray &bytes)
		{
			QTest::newRow(qPrintable(QString("%1-%2").arg(big ? "BE" : "LE", name))) << bytes;
		};
		auto broken = full;
		TestAvb::replaceU32(broken, d.countOffset, 0xffffffff, big);
		add("object-count-overflow", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.countOffset, quint32(d.objects.size() - 1), big);
		add("undeclared-trailing-object", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 0, big);
		add("null-root", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 7, big);
		add("root-out-of-range", broken);
		broken = full;
		TestAvb::replaceU32(broken, d.rootOffset, 2, big);
		add("root-is-not-bin", broken);
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
		add("original-bin-reference-out-of-range", d.bytes());
		d = graph(big);
		d.objects[3].payload = TestAvb::attributes(big, 3);
		add("original-bin-reference-wrong-type", d.bytes());
		d = graph(big);
		d.objects[0].payload = TestAvb::bin(big, {}, true);
		TestAvb::replaceU32(d.objects[0].payload, 14, 0xffffffff, big);
		add("large-bin-item-count-overflow", d.bytes());
	}
}

void TestAvbParser::malformed_counts_lengths_and_references()
{
	QFETCH(QByteArray, bytes);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("broken.avb"), bytes));
	QVERIFY(!result.valid);
	QVERIFY(!result.complete);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mobIds.isEmpty());
	QVERIFY(result.mobs.isEmpty());
}

void TestAvbParser::native_legacy_words_are_decoded_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("little-endian") << false;
	QTest::newRow("big-endian") << true;
}

void TestAvbParser::native_legacy_words_are_decoded()
{
	QFETCH(bool, big);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(big);
	d.objects[1].payload = TestAvb::composition(big, TestAvb::Master, "Old clip", 4, {3}, 0, false);
	d.objects[2].payload = TestAvb::sourceClip(big, TestAvb::Source, false);
	d.objects[5].payload = TestAvb::mobReference(big, TestAvb::Other, false);
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("native-words.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(result.complete);
	QCOMPARE(result.mobIds, aliases({legacy(TestAvb::Master), legacy(TestAvb::Source), legacy(TestAvb::Other)}));
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
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("null-source.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(result.complete);
	QCOMPARE(result.mobIds, aliases({TestAvb::Master}));
}

void TestAvbParser::unknown_class_is_incomplete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[2] = {"ZZZZ", QByteArray::fromHex("0201") + TestAvb::Source.toHex() + QByteArray(1, '\x03')};
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("unknown-dependency.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(!result.complete);
	QVERIFY(!result.warnings.isEmpty());
	QVERIFY(result.warnings.join(';').contains(QStringLiteral("ZZZZ")));
	QVERIFY(!result.mobIds.contains(MobId::format(TestAvb::Source)));
}

void TestAvbParser::typed_mob_field_framing_is_validated()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	// The final CMPO extension consists of its two-byte key, 49-byte typed
	// value and object terminator. Corrupt the label array's declared size.
	auto &payload = d.objects[1].payload;
	TestAvb::replaceU32(payload, payload.size() - 49, 0x7fffffff);
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("bad-mob-array.avb"), d.bytes()));
	QVERIFY(!result.valid);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mobIds.isEmpty());
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
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("new-extension.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(!result.complete);
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
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("dependency.avb"), bytes));
	QCOMPARE(result.valid, valid);
	QCOMPARE(result.complete, valid);
	if (valid)
		QCOMPARE(result.mobIds, aliases({TestAvb::Master, TestAvb::Source, TestAvb::Other}));
	else
	{
		QVERIFY(!result.error.isEmpty());
		QVERIFY(result.mobIds.isEmpty());
	}
}

void TestAvbParser::unknown_component_version_is_incomplete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	auto d = graph(false);
	d.objects[2] = {"SEQU", TestAvb::sequence(false, {7})};
	d.objects.append({"SCLP", TestAvb::sourceClip(false)});
	d.objects[2].payload[1] = '\x7f';
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("new-component.avb"), d.bytes()));
	QVERIFY2(result.valid, qPrintable(result.error));
	QVERIFY(!result.complete);
	QVERIFY(!result.warnings.isEmpty());
}

void TestAvbParser::cancelled_read_never_becomes_valid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	std::atomic_bool cancelled{true};
	const auto result = AvbParser::parse(TestAvb::write(tmp.filePath("cancelled.avb"), TestAvb::masterBin()), &cancelled);
	QVERIFY(!result.valid);
	QVERIFY(!result.complete);
	QVERIFY(!result.error.isEmpty());
	QVERIFY(result.mobIds.isEmpty());
}

void TestAvbParser::omf_bins_preserve_file_and_master_identities()
{
	struct Pin
	{
		const char *file;
		const char *fileMob;
		const char *masterMob;
		const char *physicalMob;
	};
	// Exact CMPO identities from the fixtures, including the physical source
	// that the old text/raw scavenger did not inventory.
	const std::array<Pin, 2> pins{{{"WAVE(OMF).avb", "060a2b340101010101010f00130000007429976a70397047060e2b347f7f2a80",
									"060a2b340101010101010f00130000007429976a4e397047060e2b347f7f2a80",
									"060a2b340101010501010f10130000000de37d9a8412069034364a963681a3eb"},
								   {"AIFF-C(OMF).avb", "060a2b340101010101010f00130000009729976a3ec57047060e2b347f7f2a80",
									"060a2b340101010101010f00130000009729976a3dc57047060e2b347f7f2a80",
									"060a2b340101010501010f10130000008209999a841206905bd14a963681a3eb"}}};
	for (const auto &pin : pins)
	{
		const auto result = AvbParser::parse(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/bins/") + QLatin1String(pin.file));
		QVERIFY2(result.valid, qPrintable(result.error));
		QVERIFY2(result.complete, qPrintable(result.warnings.join(';')));
		QCOMPARE(result.mobIds, aliases({QByteArray::fromHex(pin.fileMob), QByteArray::fromHex(pin.masterMob),
										 QByteArray::fromHex(pin.physicalMob)}));
		QVERIFY(!result.mobs.isEmpty());
	}
}

QTEST_APPLESS_MAIN(TestAvbParser)
#include "tst_avbparser.moc"
