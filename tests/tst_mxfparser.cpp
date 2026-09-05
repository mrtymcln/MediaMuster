// Unit tests for MxfParser::codecFromEssenceLabel — the public, pure codec-UL
// lookup. Covers exact table hits, the DNxHD "bitrate depends on fps" logic,
// the family-inference fallback for unrecognised ULs, and the empty/unknown
// edges. The KLV walk itself is exercised by the fixture-backed tst_scanner.

#include "mediafile.h"
#include "mobid.h"
#include "mxfparser.h"
#include "avidusage.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <limits>

namespace
{
	// A 16-byte essence-container UL from its 32-char hex.
	QByteArray ul(const char *hex)
	{
		return QByteArray::fromHex(hex);
	}

	QByteArray u16be(quint16 v)
	{
		QByteArray b;
		b.append(char((v >> 8) & 0xff));
		b.append(char(v & 0xff));
		return b;
	}

	QByteArray utf16be(const QString &s)
	{
		QByteArray b;
		for (const QChar c : s)
		{
			const ushort u = c.unicode();
			b.append(char((u >> 8) & 0xff));
			b.append(char(u & 0xff));
		}
		return b;
	}

	// One MXF metadata Set: 16-byte key (kUlSetPrefix's 13-byte match region + a
	// set-type byte at [14]) + BER short-form length + a value carrying the package
	// UID (tag 0x4401) and the package name (tag 0x4402, UTF-16BE).
	QByteArray packageSet(quint8 setType, const QByteArray &umid32, const QString &name)
	{
		QByteArray value;
		value += QByteArray::fromHex("4401") + u16be(32) + umid32;
		const QByteArray nameBytes = utf16be(name);
		value += QByteArray::fromHex("4402") + u16be(quint16(nameBytes.size())) + nameBytes;

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101"); // 13-byte prefix region
		key.append(char(0x01));												// byte[13], arbitrary
		key.append(char(setType));											// byte[14], the set type
		key.append(char(0x00));												// byte[15], arbitrary

		QByteArray out = key;
		out.append(char(value.size())); // BER short form (value < 128 bytes)
		out += value;
		return out;
	}

	QByteArray u32be(quint32 v)
	{
		QByteArray b;
		for (int i = 3; i >= 0; --i)
			b.append(char((v >> (8 * i)) & 0xff));
		return b;
	}

	// A CDCI picture-descriptor Set carrying stored width/height and, when
	// `rateNum` is non-zero, a sample-rate rational (tag 0x3001) — the field
	// the fps display string is derived from.
	QByteArray cdciSet(quint32 width, quint32 height, quint32 rateNum = 0, quint32 rateDen = 1)
	{
		QByteArray value;
		value += u16be(0x3203) + u16be(4) + u32be(width);
		value += u16be(0x3202) + u16be(4) + u32be(height);
		if (rateNum > 0)
			value += u16be(0x3001) + u16be(8) + u32be(rateNum) + u32be(rateDen);

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
		key.append(char(0x01));
		key.append(char(0x28)); // CDCI descriptor set type
		key.append(char(0x00));

		QByteArray out = key;
		out.append(char(value.size())); // BER short form
		out += value;
		return out;
	}

	// A CDCI-typed descriptor set carrying ONLY an essence label in tag
	// 0x3201 — the shape of a file whose audio-ness appears in no sound
	// descriptor set, only in the UL bytes themselves.
	QByteArray labelOnlySet(const QByteArray &label16)
	{
		QByteArray value;
		value += u16be(0x3201) + u16be(quint16(label16.size())) + label16;

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
		key.append(char(0x01));
		key.append(char(0x28)); // CDCI descriptor set type
		key.append(char(0x00));

		QByteArray out = key;
		out.append(char(value.size())); // BER short form
		out += value;
		return out;
	}

	// A well-formed KLV item with a key matching no known set: the walk steps
	// over it without parsing. Used to push later sets past the fast read.
	QByteArray fillerItem(quint16 valueLen)
	{
		QByteArray out(16, '\x11');
		out.append('\x82'); // BER long form, 2 length bytes
		out += u16be(valueLen);
		out += QByteArray(valueLen, '\0');
		return out;
	}

	QString writeMxf(const QString &path, const QByteArray &bytes)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(bytes);
			f.close();
		}
		return path;
	}
} // namespace

class TestMxfParser : public QObject
{
	Q_OBJECT
private slots:
	void avid_alpha_requires_positive_container_and_layout_data();
	void avid_alpha_requires_positive_container_and_layout();
	void avid_alpha_respects_descriptor_selection_and_primer();
	void partition_container_batch_is_bounded();
	void project_prefers_owning_file_then_material_then_linked_source();
	void project_recovers_unique_orphan_attribute();
	void project_leaves_conflicting_fallbacks_unknown();
	void empty_label_returns_empty();
	void pcm_audio_ul_resolves();
	void prores_422_ul_resolves();
	void dnxhd_bitrate_follows_fps();
	void dnxhd_hqx_carries_x_suffix();
	void vc3_720p_sq_keeps_its_own_name();
	void unknown_ul_infers_family_from_structure();
	void fully_unknown_ul_falls_back_to_hex();

	// Regression: MaterialPackage name/UMID must win even when a SourcePackage
	// comes first in the header (the "wrong clip name" bug report).
	void material_package_name_wins_over_source_package();
	void source_only_name_is_flagged_as_fallback();

	// Hardening: malformed BER lengths in the KLV walk must never crash or
	// read out of bounds — a corrupt/hostile .mxf is parsed all the time
	// because the scanner opens every file it finds.
	void hostile_ber_length_does_not_crash();
	void indefinite_ber_length_is_rejected();
	void truncated_ber_length_is_rejected();

	// Avid writes 256 KB or 512 KB headers depending on the MC version. On
	// the 512 KB flavour the descriptors can sit inside the 256 KB fast
	// read while the MaterialPackage lies beyond it; a first pass that
	// stops at "valid" silently loses the UMID and clip name.
	void material_package_beyond_fast_read_is_recovered();

	// Every fractional rate Media Composer can produce must render as its
	// exact fraction, never rounded to the nearest integer. 47.952 and
	// 119.88 used to display as "48" and "120" with no warning.
	void fractional_frame_rates_are_never_rounded();

	// Rates must be recognised by the speed the fraction works out to, not
	// the digits used to write it: 60000/2002 and 2997/100 are both 29.97,
	// but the exact-digit whitelist dropped them into the round-to-integer
	// fallback and the FPS column silently read "30". A rate matching no
	// known family must display its real value, never a rounded neighbour.
	void equivalent_rate_fractions_resolve_by_value();

	// Duration fields are big-endian integers whose recorded length varies
	// by flavour. The old reader handled only 4 and 8 exactly; a 5-7 byte
	// field read its FIRST four bytes — the value divided by 2^16/2^24 —
	// silently shrinking (usually zeroing) the duration.
	void odd_width_duration_fields_read_exactly();

	// Real Avid headers (MC 2025, one clip + one tone per project rate;
	// 256 KB slices under fixtures/avid_headers). Pins clip names, exact
	// frame rates, and audio properties against genuine corpus media —
	// the synthetic-buffer tests above can't drift these.
	void real_avid_headers_parse_exactly();

	// Durations are timecode matching the Avid bin, for audio as much as
	// video. History: audio durations first showed 00:00:00 (min-wins mixed
	// frame counts into the sample-count contract), then briefly wall-clock
	// HH:MM:SS; the settled rule is HH:MM:SS:FF at the clip's edit rate,
	// with the rate derived from the frame-track duration against the WAVE
	// sample count.
	void audio_duration_displays_bin_timecode();

	// Drop-frame material renders with SMPTE drop-frame counting and Avid's
	// semicolon separators; non-drop keeps colons. Duration rendering only —
	// the frame count itself never changes.
	void drop_frame_durations_render_like_avid();

	// MC-written MP2 audio (re-created tones) uses sound-descriptor set 0x5E
	// with the MPEG-audio compression UL in tag 0x3D06 — verified byte-level:
	// essence frames start FF FD (MPEG-1 Layer II sync). Unrecognised, the
	// whole file parsed invalid: the row lost every MXF-derived field —
	// Kind=Video, no codec, no duration.
	void mp2_audio_descriptor_recognised();

	// Audio-ness decided from the essence UL's byte structure alone —
	// no sound descriptor set anywhere in the header. Previously such a
	// file fell through as an invalid Kind=Video row.
	void label_only_audio_classifies_from_ul();

	// The collision guard. Avid's private DNxHD PICTURE labels share the
	// 0D…02 06 prefix with the registered AES3/BWF SOUND wrappings and
	// differ only in the trailing byte. A classifier that ignores it
	// calls four legacy video codecs "audio" and zeroes their duration.
	// Every colliding UL is pinned here, audio and video alike.
	void essence_label_audio_classification();

	// Quant-bits 254 is Avid's sentinel for the non-integer DNxUncompressed
	// formats. "254-bit" is not a bit depth. (The two formats ARE
	// distinguishable by UL — 16(2.14) uses 03070200 — see the corpus test
	// below; the shared "Float" bits label stays for the 03070100+254 case.)
	void float_bit_depth_sentinel_shows_float();

	// Ground-truth codec entries from the 2026-07 UHD/1080i corpus (every
	// clip named in Avid after the codec menu entry that made it; 512 KB
	// header slices). Pins the DNxHR relabel — UL ...71250000 is HQX
	// (12-bit proves it), NOT SQ as first assumed — and seven entries that
	// resolved as "unknown variant" before: DNxHR SQ/LB, the MC2025 HD
	// DNxHD SQ flavour, J2K IMF, XAVC HD Class 200, DVCPro HD, and
	// DNxUncompressed 16(2.14) via its own UL.
	void uhd_corpus_codec_entries_resolve();

	// The AVC/XAVC family shares coding ULs across marketing names; the
	// sub-descriptor (set 0x6E: level, profile, avg/max bitrate) recovers
	// the Avid menu name. One pair is identical everywhere — AVC-Intra 100
	// and XAVC HD Class 100 are the same operating point under two brands —
	// and displays the ruled combined label. All expectations verified
	// against the named corpus's complete sub-descriptor matrix.
	void avc_files_resolve_to_their_family();

	// Tripwire over the archived ground-truth corpus (fixtures/
	// corpus_headers: 512 KB slices of all 435 files, captured 2026-07-31 —
	// see its README). Every slice must keep parsing valid and must never
	// resolve to an "unknown variant" label: a failure here means either a
	// parser regression or a dictionary gap, both of which the named-corpus
	// convention exists to catch.
	void archived_corpus_all_parses_with_no_unknowns();

	// Shared derivations — the statics the MDB producer reuses.
	void applyEditRate_labels_fractional_rates();
	void mdb_style_metadata_finalises_like_a_header();
	void tagged_values_yield_source_path_and_import_flag();
	void metadata_beyond_512k_and_essence_are_not_bulk_read();
	void dynamic_primer_tags_are_resolved_by_property();
	void usage_requires_unambiguous_master_evidence_data();
	void usage_requires_unambiguous_master_evidence();
	void private_usage_requires_primer_and_exact_integer();
	void owning_package_selects_descriptor();
	void split_master_uses_individual_file_duration();
	void malformed_local_property_invalidates_header();
	void finalise_is_idempotent_and_does_not_guess();
};

void TestMxfParser::empty_label_returns_empty()
{
	QCOMPARE(MxfParser::codecFromEssenceLabel({}, QStringLiteral("25")), QString());
}

void TestMxfParser::pcm_audio_ul_resolves()
{
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B34040101010D01030102060100"), QString()),
			 QStringLiteral("PCM"));
}

void TestMxfParser::prores_422_ul_resolves()
{
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B34040101010D010301020C0301"), QString()),
			 QStringLiteral("Apple ProRes 422"));
}

void TestMxfParser::dnxhd_bitrate_follows_fps()
{
	// Same UL (DNxHD SQ tier); current Avid branding leads, the legacy
	// technical bitrate name (rate-dependent) stays in the parenthesis.
	const QByteArray sq = ul("060E2B34040101010D01030102060101");
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ (DNxHD 120)"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx SQ (DNxHD 145)"));
	// Unsupported or absent fps retains the known tier without a guessed bitrate.
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, QStringLiteral("48")),
			 QStringLiteral("Avid DNx SQ"));
}

void TestMxfParser::dnxhd_hqx_carries_x_suffix()
{
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B34040101010D01030102060202"),
											  QStringLiteral("25")),
			 QStringLiteral("Avid DNx HQX (DNxHD 185X)"));
}

void TestMxfParser::vc3_720p_sq_keeps_its_own_name()
{
	// CID 1252 (UL byte 0x12): 720p 8-bit 4:2:2 SQ. Its technical bitrate
	// names come from the 2012 whitepaper's 720p table — NOT the 1080-line
	// numbers (75 at 29.97, not 145). Display leads with current branding.
	const QByteArray sq720 = ul("060E2B340401010A0401020271120000");
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq720, QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx SQ (DNxHD 75)"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq720, QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ (DNxHD 60)"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq720, QStringLiteral("23.976")),
			 QStringLiteral("Avid DNx SQ (DNxHD 60)"));
	// The 720p 50/59.94 rows are at the bottom of p9, before p10.
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq720, QStringLiteral("59.94")),
			 QStringLiteral("Avid DNx SQ (DNxHD 145)"));

	// Sibling 720p tiers (CIDs 1251/1250), same whitepaper table.
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B340401010A0401020271110000"),
											  QStringLiteral("25")),
			 QStringLiteral("Avid DNx HQ (DNxHD 90)"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B340401010A0401020271100000"),
											  QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx HQX (DNxHD 110x)"));

	// DNxHR is under the same brand with no bitrate names — level only,
	// bare at every rate ("HR" is communicated by the Resolution column).
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B34040101010D01030102110201"),
											  QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(ul("060E2B34040101010D01030102110501"),
											  QStringLiteral("50")),
			 QStringLiteral("Avid DNx 444"));
}

void TestMxfParser::unknown_ul_infers_family_from_structure()
{
	// Not in the table, but byte[8]=04, byte[9]=01, byte[12]=71 → VC-3 family.
	const QString name =
		MxfParser::codecFromEssenceLabel(ul("060E2B340401010A0401020271FF0000"), QStringLiteral("25"));
	QVERIFY2(name.startsWith(QStringLiteral("VC-3")), qPrintable(name));
	QVERIFY(name.contains(QStringLiteral("unknown variant")));
}

void TestMxfParser::fully_unknown_ul_falls_back_to_hex()
{
	const QString name =
		MxfParser::codecFromEssenceLabel(ul("112233445566778899AABBCCDDEEFF00"), QString());
	QVERIFY2(name.startsWith(QStringLiteral("Unknown (")), qPrintable(name));
}

void TestMxfParser::material_package_name_wins_over_source_package()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Distinct UMIDs so we can tell which package the result came from.
	const QByteArray srcUmid = QByteArray::fromHex("060a2b3401010105"
												   "01010f1013000000"
												   "1111111111111111"
												   "2222222222222222");
	const QByteArray matUmid = QByteArray::fromHex("060a2b3401010105"
												   "01010f1013000000"
												   "aaaaaaaaaaaaaaaa"
												   "bbbbbbbbbbbbbbbb");

	// The reported file's ordering: a tape SourcePackage first, then the
	// MaterialPackage holding the real clip name. First-wins would lock onto
	// the tape name; the fix makes the MaterialPackage authoritative.
	QByteArray buf;
	buf += packageSet(0x37, srcUmid, QStringLiteral("7362010SS"));	  // SourcePackage, first
	buf += packageSet(0x36, matUmid, QStringLiteral("214/1.new.01")); // MaterialPackage, second

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/clip.mxf", buf));

	QCOMPARE(meta.clipName, QStringLiteral("214/1.new.01"));
	QCOMPARE(meta.umid, MobId::format(matUmid));
	// Provenance drives scanner precedence: a material name overrides the MDB.
	QVERIFY(meta.clipNameFromMaterial);
}

void TestMxfParser::source_only_name_is_flagged_as_fallback()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QByteArray srcUmid = QByteArray::fromHex("060a2b3401010105"
												   "01010f1013000000"
												   "1111111111111111"
												   "2222222222222222");

	// Only a SourcePackage carries a name: it's used, but flagged non-material
	// so the scanner won't let it override the MDB's clip name.
	QByteArray buf;
	buf += packageSet(0x37, srcUmid, QStringLiteral("7302108SL"));

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/clip.mxf", buf));

	QCOMPARE(meta.clipName, QStringLiteral("7302108SL"));
	QVERIFY(!meta.clipNameFromMaterial);
}

void TestMxfParser::hostile_ber_length_does_not_crash()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// 16-byte key, then a BER long-form length claiming 0x7FFFFFFFFFFFFFFF
	// bytes. Before the overflow-safe bounds check, `valuePos + length`
	// wrapped negative, slipped past the `> dataSize` guard, drove `pos`
	// negative, and the next read ran off the front of the buffer. The parser
	// must now bail cleanly rather than crash.
	QByteArray buf(16, '\x11'); // arbitrary key (not the header-partition UL)
	buf.append('\x88');			// long form: 8 length bytes follow
	buf.append(QByteArray::fromHex("7FFFFFFFFFFFFFFF"));

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/hostile.mxf", buf));
	QVERIFY(!meta.valid);
}

void TestMxfParser::indefinite_ber_length_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// 0x80 is BER indefinite length (illegal in MXF). It must be rejected as
	// malformed and stop the walk — not read as "length 0", which would
	// desync the parser into the following bytes.
	QByteArray buf(16, '\x11');
	buf.append('\x80');					// indefinite length
	buf.append(QByteArray(64, '\x00')); // trailing bytes that must not be walked

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/indef.mxf", buf));
	QVERIFY(!meta.valid);
}

void TestMxfParser::truncated_ber_length_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Long-form byte says "4 length bytes follow" but the buffer ends first.
	// readBerLength must reject it (offset + 1 + lenBytes > size), not read
	// past the end.
	QByteArray buf(16, '\x11');
	buf.append('\x84'); // long form, 4 length bytes...
	buf.append('\x00'); // ...only 1 present

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/trunc.mxf", buf));
	QVERIFY(!meta.valid);
}

void TestMxfParser::material_package_beyond_fast_read_is_recovered()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QByteArray matUmid = QByteArray::fromHex("060a2b3401010105"
												   "01010f1013000000"
												   "cccccccccccccccc"
												   "dddddddddddddddd");

	// Descriptors early, MaterialPackage pushed past the 256 KB fast read
	// by filler items; total stays well under the 512 KB ceiling.
	QByteArray buf = cdciSet(1920, 1080);
	while (buf.size() < 265 * 1024)
		buf += fillerItem(8000);
	buf += packageSet(0x36, matUmid, QStringLiteral("BEYOND/1.new.01"));

	const MxfMetadata meta = MxfParser::parseHeader(writeMxf(tmp.path() + "/big.mxf", buf));

	QCOMPARE(meta.resolution, QStringLiteral("1920x1080"));
	QCOMPARE(meta.clipName, QStringLiteral("BEYOND/1.new.01"));
	QVERIFY(meta.clipNameFromMaterial);
	QCOMPARE(meta.umid, MobId::format(matUmid));
}

void TestMxfParser::fractional_frame_rates_are_never_rounded()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// num/den as written in tag 0x3001 → the exact display string required.
	const struct
	{
		quint32 num, den;
		const char *expected;
	} kRates[] = {
		{24000, 1001, "23.976"},
		{30000, 1001, "29.97"},
		{60000, 1001, "59.94"},
		{48000, 1001, "47.952"},  // MC v8.3+; used to round to "48"
		{120000, 1001, "119.88"}, // MC 2018.7+; used to round to "120"
		{25, 1, "25"},			  // integer control — qRound path stays exact
	};

	for (const auto &r : kRates)
	{
		const QString path =
			tmp.path() + QStringLiteral("/rate_%1_%2.mxf").arg(r.num).arg(r.den);
		const MxfMetadata meta =
			MxfParser::parseHeader(writeMxf(path, cdciSet(1920, 1080, r.num, r.den)));
		QVERIFY(meta.valid);
		QCOMPARE(meta.fps, QString::fromLatin1(r.expected));
	}
}

void TestMxfParser::equivalent_rate_fractions_resolve_by_value()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const struct
	{
		quint32 num, den;
		const char *expected;
	} kRates[] = {
		{60000, 2002, "29.97"},	 // unreduced NTSC fraction — used to read "30"
		{2997, 100, "29.97"},	 // decimal-approximation muxers — used to read "30"
		{2398, 100, "23.976"},	 // 23.98 spelling of 23.976 — used to read "24"
		{5994, 100, "59.94"},	 // decimal spelling — used to read "60"
		{25000, 1000, "25"},	 // scaled integer stays an integer
		{31, 2, "15.5"},		 // no known family: show the real value, not "16"
	};

	for (const auto &r : kRates)
	{
		const QString path =
			tmp.path() + QStringLiteral("/eqrate_%1_%2.mxf").arg(r.num).arg(r.den);
		const MxfMetadata meta =
			MxfParser::parseHeader(writeMxf(path, cdciSet(1920, 1080, r.num, r.den)));
		QVERIFY(meta.valid);
		QCOMPARE(meta.fps, QString::fromLatin1(r.expected));
	}
}

void TestMxfParser::odd_width_duration_fields_read_exactly()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A CDCI descriptor whose ContainerDuration (0x3002) is recorded as SIX
	// bytes: big-endian 300 = 00 00 00 00 01 2C.
	QByteArray value;
	value += u16be(0x3203) + u16be(4) + u32be(1920);
	value += u16be(0x3202) + u16be(4) + u32be(1080);
	value += u16be(0x3002) + u16be(6) + QByteArray::fromHex("00000000012c");

	QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
	key.append(char(0x01));
	key.append(char(0x28)); // CDCI descriptor set type
	key.append(char(0x00));
	QByteArray set = key;
	set.append(char(value.size())); // BER short form
	set += value;

	const MxfMetadata meta =
		MxfParser::parseHeader(writeMxf(tmp.path() + "/sixbyte.mxf", set));
	QVERIFY(meta.valid);
	QCOMPARE(meta.durationFrames, qint64(300));
}

void TestMxfParser::mp2_audio_descriptor_recognised()
{
	const QString path = QStringLiteral(
		FIXTURES_DIR "/avid_headers/A01.E68C35B3_2C34B2C34B61AA.mxf");
	QVERIFY(QFile::exists(path));
	const MxfMetadata m = MxfParser::parseHeader(path);

	QVERIFY(m.valid);
	QVERIFY(m.isAudio);
	QCOMPARE(m.codec, QStringLiteral("MP2"));
	QCOMPARE(m.sampleRate, 48000);
	QCOMPARE(m.channels, 1);
	QCOMPARE(m.bitDepth, QStringLiteral("16-bit"));
	QCOMPARE(m.clipName, QStringLiteral("TONE: 1000 Hz @ -20.0 dB.4.new.10"));
	QVERIFY(m.clipNameFromMaterial);
	QCOMPARE(m.durationFrames, qint64(1800));
	QCOMPARE(m.timecodeBase, 30); // 1800 frames vs 2,883,456 samples @48k → 29.96 → 30

	MediaFile mf;
	mf.kind = MediaFile::Kind::Audio;
	mf.durationFrames = m.durationFrames;
	mf.timecodeBase = m.timecodeBase;
	QCOMPARE(mf.durationDisplay(), QStringLiteral("00:01:00:00"));
}

void TestMxfParser::label_only_audio_classifies_from_ul()
{
	// The parser must classify from the UL, name the codec, and leave
	// the duration honestly blank (nothing to derive a rate from).
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// GC AES3/BWF sound container — the namespace of Avid's PCM label.
	const MxfMetadata pcm = MxfParser::parseHeader(writeMxf(
		tmp.path() + "/pcm.mxf", labelOnlySet(ul("060E2B34040101010D01030102060100"))));
	QVERIFY(pcm.valid);
	QVERIFY(pcm.isAudio);
	QCOMPARE(pcm.codec, QStringLiteral("PCM"));
	QCOMPARE(pcm.durationFrames, qint64(0));

	// MPEG-1 Layer II sound-coding UL (bytes 8-9 = 04 02).
	const MxfMetadata mp2 = MxfParser::parseHeader(writeMxf(
		tmp.path() + "/mp2.mxf", labelOnlySet(ul("060E2B34040101010402020203020500"))));
	QVERIFY(mp2.valid);
	QVERIFY(mp2.isAudio);
	QCOMPARE(mp2.codec, QStringLiteral("MP2"));

	// A video coding UL through the identical shape must NOT classify
	// as audio (DNxHD SQ; picture namespace 04 01).
	const MxfMetadata vid = MxfParser::parseHeader(writeMxf(
		tmp.path() + "/vid.mxf", labelOnlySet(ul("060E2B340401010A0401020271030000"))));
	QVERIFY(!vid.isAudio);
}

void TestMxfParser::essence_label_audio_classification()
{
	// Label-only headers (no descriptor set) so the UL alone decides.
	// The four DNxHD entries are the trap: same 0D…02 06 prefix as the
	// PCM sound wrapping, differing only at the last byte.
	const struct
	{
		const char *ul;
		bool audio;
		const char *why;
	} kCases[] = {
		{"060E2B34040101010D01030102060100", true, "PCM: registered BWF sound wrapping (ends 00)"},
		{"060E2B34040101010402020203020500", true, "MP2: SMPTE sound coding node (04 02)"},
		{"060E2B34040101010D01030102060101", false, "DNxHD SQ: Avid private PICTURE label"},
		{"060E2B34040101010D01030102060201", false, "DNxHD HQ: Avid private PICTURE label"},
		{"060E2B34040101010D01030102060202", false, "DNxHD HQX: Avid private PICTURE label"},
		{"060E2B34040101010D01030102060301", false, "DNxHD LB: Avid private PICTURE label"},
		{"060E2B340401010A0401020271030000", false, "DNxHD SQ via CID UL: picture coding (04 01)"},
		{"060E2B34040101010D01030102050101", false, "Avid 1:1 8-bit: picture"},
		{"060E2B340401010D0401020203060300", false, "Apple ProRes 422: picture"},
	};

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	int i = 0;
	for (const auto &c : kCases)
	{
		const QString path = tmp.path() + QStringLiteral("/case%1.mxf").arg(i++);
		const MxfMetadata m = MxfParser::parseHeader(writeMxf(path, labelOnlySet(ul(c.ul))));
		// The label must actually have reached the classifier, or the
		// isAudio assertion below would pass vacuously.
		QVERIFY2(!m.essenceContainerLabel.isEmpty(), c.why);
		QVERIFY2(m.isAudio == c.audio, c.why);
	}
}

void TestMxfParser::float_bit_depth_sentinel_shows_float()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const struct
	{
		quint32 bits;
		const char *expected;
	} kCases[] = {
		{10, "10-bit"},  // integer depths unchanged
		{254, "Float"},  // sentinel — used to read "254-bit"
	};

	for (const auto &c : kCases)
	{
		QByteArray value;
		value += u16be(0x3203) + u16be(4) + u32be(1920);
		value += u16be(0x3202) + u16be(4) + u32be(1080);
		value += u16be(0x3301) + u16be(4) + u32be(c.bits);

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
		key.append(char(0x01));
		key.append(char(0x28)); // CDCI descriptor set type
		key.append(char(0x00));
		QByteArray set = key;
		set.append(char(value.size()));
		set += value;

		const MxfMetadata meta = MxfParser::parseHeader(
			writeMxf(tmp.path() + QStringLiteral("/bits_%1.mxf").arg(c.bits), set));
		QVERIFY(meta.valid);
		QCOMPARE(meta.bitDepth, QString::fromLatin1(c.expected));
	}
}

void TestMxfParser::uhd_corpus_codec_entries_resolve()
{
	const struct
	{
		const char *name;
		const char *codec;
		const char *bits;
	} kFiles[] = {
		// UHD DNxHR tiers (SMPTE CIDs: 0x25=HQX, 0x26=HQ, 0x27=SQ, 0x28=LB)
		{"V01.E68C0541_77627077627DBV.mxf", "Avid DNx HQX", "12-bit"},
		{"V01.E68C04FE_7430E07430E4AV.mxf", "Avid DNx SQ", "8-bit"},
		{"V01.E68C04CC_71CC7071CC76BV.mxf", "Avid DNx LB", "8-bit"},
		// MC 2025's HD SQ flavour (Avid-private namespace, CID byte 0x08),
		// 1080i50 — the DNxHD bitrate table applies: SQ at 25 fps = 120.
		{"V01.E68E7FE8_7A59107A591A7V.mxf", "Avid DNx SQ (DNxHD 120)", "8-bit"},
		{"V01.E68C0302_5C17705C17776V.mxf", "JPEG 2000 IMF", "10-bit"},
		{"V01.E690E7A6_DAB26DAB26AE8V.mxf", "AVC Intra", "10-bit"},
		// Must stay exactly "DV 1080 50i" — the legacy "DV " display branch
		// (which appends i(PAL)/p(NTSC)) must not fire on it.
		{"V01.E690E9D6_DC5B9DC5B9983V.mxf", "DV 1080 50i", "10-bit"},
		// 16-bit 2.14 fixed point has its OWN UL (03070200); quant-bits
		// still reads 254 → "Float".
		{"V01.E68C029A_5733205733282V.mxf", "Avid DNxUncompressed 2.14", "Float"},
		// Round-3 corpus: NTSC-flavour ULs for XAVC HD C200, DVCPro HD,
		// and the classic DV codecs (which take the i(NTSC) display
		// suffix; DVCPro HD deliberately does not).
		{"V01.E6967DC1_1705D1705D0ADV.mxf", "AVC Intra", "10-bit"},
		{"V01.E696842B_1BE361BE366C3V.mxf", "DV 1080 60i", "10-bit"},
		{"V01.E69CEDE0_F926CF926C983V.mxf", "DV NTSC 25Mbps 4:1:1", "8-bit"},
		{"V01.E69CEDFB_F93B5F93B54A0V.mxf", "DV NTSC 50Mbps 4:2:2", "8-bit"},
	};

	for (const auto &f : kFiles)
	{
		const QString path = QStringLiteral(FIXTURES_DIR "/avid_headers/") + QLatin1String(f.name);
		QVERIFY2(QFile::exists(path), f.name);
		const MxfMetadata meta = MxfParser::parseHeader(path);
		QVERIFY2(meta.valid, f.name);
		QCOMPARE(meta.codec, QString::fromLatin1(f.codec));
		QCOMPARE(meta.bitDepth, QString::fromLatin1(f.bits));
	}
}

void TestMxfParser::avc_files_resolve_to_their_family()
{
	// AVC is named at Avid's own codec_family level ("AVC Intra",
	// "AVC Long GOP", "H.264"). The bitrate class is NOT in the essence
	// label — one UL legitimately covers AVC Long GOP 6/12/25/35/50 — and
	// the raster is shown in its own column, so the Codec column states the
	// family and lets Resolution and Size carry the rest (ruled 2026-08-13).
	//
	// These 15 real headers span both brands, both intra and long-GOP, HD
	// and 4K. What they pin is that every one is RECOGNISED: none falls
	// through to "unknown variant", and none is mistaken for another family.
	const struct
	{
		const char *name;
		const char *codec;
	} kFiles[] = {
		// Intra, HD — the permanent tie: both brands, one identical stream.
		{"V01.E68E83FB_ABC890ABC894BV.mxf", "AVC Intra"}, // made via AVC-Intra 100
		{"V01.E68E854D_BBD2E0BBD2E76V.mxf", "AVC Intra"}, // made via XAVC HD C100
		{"V01.E6967670_1178C1178C48BV.mxf", "AVC Intra"},
		{"V01.E69676B7_11AF211AF2D58V.mxf", "AVC Intra"},
		{"V01.E696883A_1EF721EF72BD8V.mxf", "AVC Intra"},
		{"V01.E69CB19C_CB708CB708DF5V.mxf", "AVC Intra"},
		// Intra, HD 4:2:2 on the shared UL, no level in the sub-descriptor.
		{"V01.E6968EE0_2402924029668V.mxf", "AVC Intra"},
		// Intra, 4K — Sony classes 300/480, CBG and VBR, plus Panasonic 4K.
		{"V01.E68BFF59_2FAA002FAA0C1V.mxf", "AVC Intra"},
		{"V01.E68C08A3_A069D0A069D96V.mxf", "AVC Intra"},
		{"V01.E68BFF99_32B33032B332CV.mxf", "AVC Intra"},
		{"V01.E68C00C8_4110D04110DC8V.mxf", "AVC Intra"},
		{"V01.E68C011B_45086045086F3V.mxf", "AVC Intra"},
		// Long GOP — 6, 25 and 50 Mbps all share one label by design.
		{"V01.E68E8578_BDD530BDD5350V.mxf", "AVC Long GOP"},
		{"V01.E68E8618_C56A60C56A6CAV.mxf", "AVC Long GOP"},
		{"V01.E68E8691_CB2C80CB2C86CV.mxf", "AVC Long GOP"},
	};

	for (const auto &f : kFiles)
	{
		const QString path = QStringLiteral(FIXTURES_DIR "/corpus_headers/") + QLatin1String(f.name);
		QVERIFY2(QFile::exists(path), qPrintable(path));
		const MxfMetadata meta = MxfParser::parseHeader(path);
		QVERIFY2(meta.valid, f.name);
		QCOMPARE(meta.codec, QString::fromLatin1(f.codec));
	}
}

void TestMxfParser::archived_corpus_all_parses_with_no_unknowns()
{
	QDir corpus(QStringLiteral(FIXTURES_DIR "/corpus_headers"));
	QVERIFY2(corpus.exists(), "corpus_headers archive missing");

	const QStringList slices = corpus.entryList({QStringLiteral("*.mxf")}, QDir::Files);
	QVERIFY2(slices.size() >= 795, qPrintable(QStringLiteral("expected the full archive, found %1")
												  .arg(slices.size())));

	int checked = 0;
	for (const QString &name : slices)
	{
		const MxfMetadata meta = MxfParser::parseHeader(corpus.filePath(name));
		QVERIFY2(meta.valid, qPrintable(name));
		QVERIFY2(!meta.codec.isEmpty(), qPrintable(name));
		QVERIFY2(!meta.codec.contains(QLatin1String("unknown variant")),
				 qPrintable(name + QStringLiteral(" -> ") + meta.codec));
		++checked;
	}
	QCOMPARE(checked, slices.size());
}

void TestMxfParser::real_avid_headers_parse_exactly()
{
	// name, expected MaterialPackage clip name, expected fps ("" = audio,
	// which never sets fps), expected resolved codec, expected isAudio,
	// expected durationFrames, expected timecodeBase. Durations are FRAMES
	// at the clip's edit rate for video AND audio — the Avid-bin timecode
	// model. Audio's base is derived by the parser from the frame-track
	// duration against the WAVE sample count, so it's pinned per file here.
	// The video codec pins UL 71.12 (DNxHD SQ at 720p) against real files —
	// it displayed as "VC-3 (unknown variant)" until 2026-07.
	const struct
	{
		const char *name;
		const char *clip;
		const char *fps;
		const char *codec;
		bool isAudio;
		qint64 duration;
		int base;
	} kFiles[] = {
		// 23.976 project
		{"V01.E683C412_F82F4F82F461DV.mxf", "file_example.new.01", "23.976",
		 "Avid DNx SQ (DNxHD 60)", false, 733, 24},
		{"A01.E683C413_F82F4F82F4625A.mxf", "file_example.new.01", "", "PCM", true, 733, 24},
		{"A02.E683C414_F82F4F82F462CA.mxf", "file_example.new.01", "", "PCM", true, 733, 24},
		{"TONE_100A01.F7C83BB3.612410.mxf", "TONE: 1000 Hz @ -14.0 dB.1", "", "PCM", true,
		 1440, 24},
		// 25 project
		{"V01.E683CD72_FF4BEFF4BE92DV.mxf", "file_example.new.02", "25",
		 "Avid DNx SQ (DNxHD 60)", false, 765, 25},
		{"A01.E683CD73_FF4BEFF4BE934A.mxf", "file_example.new.02", "", "PCM", true, 765, 25},
		{"A02.E683CD74_FF4BEFF4BE93AA.mxf", "file_example.new.02", "", "PCM", true, 765, 25},
		{"TONE_100A01.FF2DC746.612410.mxf", "TONE: 1000 Hz @ -20.0 dB.2", "", "PCM", true,
		 1500, 25},
		// 29.97 project
		{"V01.E683CDD2_FF944FF944C9AV.mxf", "file_example", "29.97",
		 "Avid DNx SQ (DNxHD 75)", false, 917, 30},
		{"A01.E683CDD3_FF944FF944CA8A.mxf", "file_example", "", "PCM", true, 917, 30},
		{"A02.E683CDD4_FF944FF944CAEA.mxf", "file_example", "", "PCM", true, 917, 30},
		{"TONE_100A01.FF8A308A.612410.mxf", "TONE: 1000 Hz @ -20.0 dB.3", "", "PCM", true,
		 1800, 30},
		// The complete codec menu of a 720p/25 MC 2025 project, one clip per
		// option (all transcodes of the same source).
		{"V01.E6842930_44E2544E25602V.mxf", "file_example.new.01", "25",
		 "Avid DNx HQ (DNxHD 90)", false, 765, 25},
		{"V01.E684294C_44F7C44F7C4E0V.mxf", "file_example.new.02", "25",
		 "Avid DNx HQX (DNxHD 90x)", false, 765, 25},
		{"V01.E6842967_450BD450BDEAAV.mxf", "file_example.new.03", "25",
		 "Apple ProRes Proxy", false, 765, 25},
		{"V01.E6842979_451A2451A2C16V.mxf", "file_example.new.04", "25",
		 "Apple ProRes LT", false, 765, 25},
		{"V01.E6842987_452474524781CV.mxf", "file_example.new.05", "25",
		 "Apple ProRes 422", false, 765, 25},
		{"V01.E684299A_4533345333DF2V.mxf", "file_example.new.06", "25",
		 "Apple ProRes HQ", false, 765, 25},
		{"V01.E68429AB_453F7453F7529V.mxf", "file_example.new.07", "25",
		 "AVC Intra", false, 765, 25},
		{"V01.E68429BA_454B5454B572DV.mxf", "file_example.new.08", "25",
		 "H.264", false, 765, 25},
		{"V01.E68429CD_4559A4559A7E8V.mxf", "file_example.new.09", "25",
		 "XDCAM EX 35", false, 765, 25},
		{"V01.E68429DC_45654456547D5V.mxf", "file_example.new.10", "25",
		 "XDCAM HD 50", false, 765, 25},
		{"V01.E68429EE_4572545725368V.mxf", "file_example.new.11", "25",
		 "Avid DNxUncompressed", false, 765, 25},
		{"V01.E6842A05_4584645846054V.mxf", "file_example.new.12", "25",
		 "Avid DNxUncompressed", false, 765, 25},
		{"V01.E6842A16_4591145911928V.mxf", "file_example.new.13", "25",
		 "Avid DNxUncompressed", false, 765, 25},
		{"V01.E6842A2B_45A0B45A0BF4DV.mxf", "file_example.new.14", "25",
		 "Avid DNxUncompressed", false, 765, 25},
		{"V01.E6842A40_45B1045B10D32V.mxf", "file_example.new.15", "25",
		 "Avid DNxUncompressed 2.14", false, 765, 25},
		{"V01.E6842A5F_45C8645C869E2V.mxf", "file_example.new.16", "25",
		 "J2K HD", false, 765, 25},
	};

	for (const auto &f : kFiles)
	{
		const QString path = QStringLiteral(FIXTURES_DIR "/avid_headers/") + QLatin1String(f.name);
		QVERIFY2(QFile::exists(path), qPrintable(path));
		const MxfMetadata meta = MxfParser::parseHeader(path);

		QVERIFY2(meta.valid, f.name);
		QCOMPARE(meta.clipName, QString::fromLatin1(f.clip));
		QVERIFY2(meta.clipNameFromMaterial, f.name);
		QCOMPARE(meta.fps, QString::fromLatin1(f.fps));
		QCOMPARE(meta.codec, QString::fromLatin1(f.codec));
		QCOMPARE(meta.isAudio, f.isAudio);
		if (f.isAudio)
			QCOMPARE(meta.sampleRate, 48000);
		QCOMPARE(meta.durationFrames, f.duration);
		QCOMPARE(meta.timecodeBase, f.base);
	}
}

void TestMxfParser::audio_duration_displays_bin_timecode()
{
	// End to end through the same steps the scanner takes: parse the real
	// header, copy the fields applyMxfMetadata copies, and format with
	// MediaFile::durationDisplay. Audio shows HH:MM:SS:FF at the clip's
	// edit rate, exactly like the Avid bin — never wall clock.
	const struct
	{
		const char *name;
		const char *display;
	} kAudio[] = {
		{"A01.E683CD73_FF4BEFF4BE934A.mxf", "00:00:30:15"}, // 765 frames @ 25
		{"A01.E683C413_F82F4F82F4625A.mxf", "00:00:30:13"}, // 733 frames @ 24 (23.976)
		{"TONE_100A01.FF2DC746.612410.mxf", "00:01:00:00"}, // 1500 frames @ 25
	};

	for (const auto &a : kAudio)
	{
		const QString path = QStringLiteral(FIXTURES_DIR "/avid_headers/") + QLatin1String(a.name);
		const MxfMetadata meta = MxfParser::parseHeader(path);
		QVERIFY2(meta.valid && meta.isAudio, a.name);

		MediaFile mf;
		mf.kind = MediaFile::Kind::Audio;
		if (meta.durationFrames > 0)
			mf.durationFrames = meta.durationFrames;
		if (meta.timecodeBase > 0)
			mf.timecodeBase = meta.timecodeBase;
		if (meta.dropFrame)
			mf.dropFrame = true;
		QCOMPARE(mf.durationDisplay(), QString::fromLatin1(a.display));
	}

	// Video through the same path: 765 frames at 25 fps.
	const MxfMetadata v = MxfParser::parseHeader(
		QStringLiteral(FIXTURES_DIR "/avid_headers/V01.E683CD72_FF4BEFF4BE92DV.mxf"));
	MediaFile vf;
	vf.fps = v.fps;
	vf.durationFrames = v.durationFrames;
	vf.timecodeBase = v.timecodeBase;
	QCOMPARE(vf.durationDisplay(), QStringLiteral("00:00:30:15"));
}

void TestMxfParser::drop_frame_durations_render_like_avid()
{
	// Straight through MediaFile::durationDisplay with hand-set fields.
	// 29.97 DF drops 2 frame numbers per minute except every tenth, so
	// 1800 frames is 00;01;00;02 (one minute of timecode ran out 2 frames
	// early) and 17982 frames is exactly ten minutes. 59.94 DF drops 4.
	const struct
	{
		qint64 frames;
		int base;
		bool drop;
		const char *display;
	} kCases[] = {
		{1800, 30, false, "00:01:00:00"},  // non-drop control
		{1800, 30, true, "00;01;00;02"},   // 29.97 DF
		{17982, 30, true, "00;10;00;00"},  // exactly ten DF minutes
		{1799, 30, true, "00;00;59;29"},   // last frame before the first drop
		{3600, 60, true, "00;01;00;04"},   // 59.94 DF drops 4
	};

	for (const auto &c : kCases)
	{
		MediaFile mf;
		mf.durationFrames = c.frames;
		mf.timecodeBase = c.base;
		mf.dropFrame = c.drop;
		QCOMPARE(mf.durationDisplay(), QString::fromLatin1(c.display));
	}

	// The real thing (2026-08-08): a genuine 29.97 DF Video Mixdown —
	// the first real media ever seen carrying DropFrame=1 in tag 0x1503.
	// 18,340 frames = one full DF ten-minute block (17,982) + 358, which
	// the synthetic cases above predicted as 00;10;11;28 before this file
	// existed.
	const QString path = QStringLiteral(
		FIXTURES_DIR "/avid_headers/Untitled Sequence.175B1728V.mxf");
	QVERIFY(QFile::exists(path));
	const MxfMetadata m = MxfParser::parseHeader(path);
	QVERIFY(m.valid);
	QVERIFY(m.dropFrame);
	QCOMPARE(m.timecodeBase, 30);
	QCOMPARE(m.durationFrames, qint64(18340));
	MediaFile mf;
	mf.durationFrames = m.durationFrames;
	mf.timecodeBase = m.timecodeBase;
	mf.dropFrame = m.dropFrame;
	QCOMPARE(mf.durationDisplay(), QStringLiteral("00;10;11;28"));
}

// MARK: - Shared derivations

// The MDB stores rates as decimal approximations (2997/100) where the MXF
// stores exact fractions (30000/1001); the label must come out identical
// whichever spelling arrives, and audio rationals go to sampleRate instead.
void TestMxfParser::applyEditRate_labels_fractional_rates()
{
	struct Case { quint32 num, den; const char *fps; int base; };
	const Case cases[] = {
		{24000, 1001, "23.976", 24}, {2997, 100, "29.97", 30}, {60000, 2002, "29.97", 30},
		{30000, 1001, "29.97", 30},  {25, 1, "25", 25},         {23976, 1000, "23.976", 24},
		{50, 1, "50", 50},           {60000, 1001, "59.94", 60},
	};
	for (const Case &c : cases)
	{
		MxfMetadata m;
		MxfParser::applyEditRate(m, c.num, c.den);
		QCOMPARE(m.fps, QString::fromLatin1(c.fps));
		QCOMPARE(m.timecodeBase, c.base);
		QCOMPARE(m.sampleRate, 0);
	}
	MxfMetadata a;
	a.isAudio = true;
	MxfParser::applyEditRate(a, 48000, 1);
	QCOMPARE(a.sampleRate, 48000);
	QVERIFY(a.fps.isEmpty());

	MxfMetadata z;
	MxfParser::applyEditRate(z, 25, 0); // zero denominator: ignored, not a crash
	QVERIFY(z.fps.isEmpty());
}

// A struct filled from msmMMOB.mdb instead of a header: already full-frame
// height, real frame layout, label, fps. finalise() must derive exactly what
// the header path derives — and must NOT double a layout-1 height twice.
void TestMxfParser::mdb_style_metadata_finalises_like_a_header()
{
	const QByteArray sq = ul("060E2B34040101010D01030102060101");

	MxfMetadata db;
	db.essenceContainerLabel = sq;
	db.width = 1920;
	db.height = 1080; // the MDB producer already doubled its 540
	db.frameLayout = 1;
	db.heightIsFrameHeight = true;
	MxfParser::applyEditRate(db, 25, 1);
	MxfParser::finalise(db);
	QVERIFY(db.valid);
	QCOMPARE(db.resolution, QStringLiteral("1920x1080"));
	QCOMPARE(db.codec, QStringLiteral("Avid DNx SQ (DNxHD 120)"));

	// The same facts as a header presents them: a field height, no flag.
	MxfMetadata hdr;
	hdr.essenceContainerLabel = sq;
	hdr.width = 1920;
	hdr.height = 540;
	hdr.frameLayout = 1;
	MxfParser::applyEditRate(hdr, 25, 1);
	MxfParser::finalise(hdr);
	QCOMPARE(hdr.resolution, db.resolution);
	QCOMPARE(hdr.codec, db.codec);
	QCOMPARE(hdr.valid, db.valid);

	// Layout 3 is a full height in a header but a half height in the MDB;
	// the producer normalises and flags, finalise leaves it alone.
	MxfMetadata l3;
	l3.essenceContainerLabel = sq;
	l3.width = 1920;
	l3.height = 1080;
	l3.frameLayout = 3;
	l3.heightIsFrameHeight = true;
	MxfParser::applyEditRate(l3, 25, 1);
	MxfParser::finalise(l3);
	QCOMPARE(l3.resolution, QStringLiteral("1920x1080"));

	// Audio from the database: samples + sample rate + frame count.
	MxfMetadata au;
	au.isAudio = true;
	au.sampleRate = 48000;
	au.pcmDescriptor = true;
	au.descriptorDuration = 2880002; // samples
	au.durationFrames = 1500;		  // frames at 25
	MxfParser::finalise(au);
	QVERIFY(au.valid);
	QCOMPARE(au.timecodeBase, 25);
	QCOMPARE(au.durationFrames, qint64(1500));
	QCOMPARE(au.codec, QString::fromLatin1(kPcmAudioName));
	QCOMPARE(MxfParser::bitDepthLabel(24), QStringLiteral("24-bit"));
	QCOMPARE(MxfParser::bitDepthLabel(254), QStringLiteral("Float"));
}

// The MaterialPackage's TaggedValues carry the import facts the MDB also
// holds — `UNC Path`, `Video`, `_IMPORTSETTING` — so an Interplay site (no
// local databases) still gets Source File and Imported from the header.
void TestMxfParser::tagged_values_yield_source_path_and_import_flag()
{
	const QString imported = QStringLiteral(FIXTURES_DIR "/corpus_headers/V01.E6966CE5_A3C580A3C588BV.mxf");
	QVERIFY(QFileInfo::exists(imported));
	const MxfMetadata m = MxfParser::parseHeader(imported);
	QVERIFY(m.valid);
	QVERIFY(m.hasImportSetting);
	QCOMPARE(m.sourceContainer, QStringLiteral("QTFF"));
	QVERIFY2(m.sourceFilePath.endsWith(QStringLiteral("/Avid DNx SQ.mov")), qPrintable(m.sourceFilePath));
	QVERIFY2(m.sourceFilePath.startsWith(QLatin1Char('/')), qPrintable(m.sourceFilePath));
	QVERIFY2(!m.projectName.isEmpty(), "the header names the project Avid created the media in");

	// `_PJ` is the attribute Media Composer's own PMR rebuild reads from the
	// file; the PMR beside this fixture says "block 1729" and so does the file.
	const MxfMetadata pmrTone =
		MxfParser::parseHeader(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"));
	QVERIFY(pmrTone.valid);
	QCOMPARE(pmrTone.projectName, QStringLiteral("block 1729"));

	// Avid-generated media carries none of the import facts: a tone and a render.
	const MxfMetadata tone =
		MxfParser::parseHeader(QStringLiteral(FIXTURES_DIR "/avid_headers/TONE_100A01.F7C83BB3.612410.mxf"));
	QVERIFY(tone.valid);
	QVERIFY(!tone.hasImportSetting);
	QVERIFY(tone.sourceFilePath.isEmpty());
	QVERIFY(tone.sourceContainer.isEmpty());

	const MxfMetadata render =
		MxfParser::parseHeader(QStringLiteral(FIXTURES_DIR "/zT_ßt_1080i_50_seqDD866C6BV.mxf"));
	QVERIFY(render.valid);
	QVERIFY(render.isPrecompute);
	QVERIFY(!render.hasImportSetting);
	QVERIFY(render.sourceFilePath.isEmpty());
}

namespace
{
QByteArray localProperty(quint16 tag, const QByteArray &value)
{
	return u16be(tag) + u16be(quint16(value.size())) + value;
}
QByteArray klv(const QByteArray &key, const QByteArray &value)
{
	return key + QByteArray(1, char(0x84)) + u32be(quint32(value.size())) + value;
}
QByteArray objectSet(quint8 type, char instance, const QByteArray &fields)
{
	QByteArray key = ul("060e2b34025301010d01010101010000");
	key[14] = char(type);
	return klv(key, localProperty(0x3c0a, QByteArray(16, instance)) + fields);
}
QByteArray references(char instance)
{
	return u32be(1) + u32be(16) + QByteArray(16, instance);
}
QByteArray partitionPack()
{
	return klv(ul("060e2b34020501010d01020101020400"), QByteArray(88, '\0'));
}
}

void TestMxfParser::metadata_beyond_512k_and_essence_are_not_bulk_read()
{
	QTemporaryDir temp;
	QByteArray header = partitionPack() + cdciSet(1920, 1080, 25);
	for (int n = 0; n < 20; ++n) header += fillerItem(60000);
	header += packageSet(0x36, QByteArray(32, 'm'), QStringLiteral("Late material"));
	// Declare a 16 MiB essence payload but keep the test file sparse.
	header += ul("060e2b34010201010d01030115010501");
	header += char(0x84);
	header += u32be(16 * 1024 * 1024);
	const QString path = writeMxf(temp.filePath("late.mxf"), header);
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadWrite));
	QVERIFY(file.resize(header.size() + 16 * 1024 * 1024));
	file.close();
	qint64 bytes = 0;
	const auto result = MxfParser::parseHeader(path, &bytes);
	QVERIFY(result.valid);
	QCOMPARE(result.headerStatus, MxfMetadata::HeaderStatus::Complete);
	QCOMPARE(result.clipName, QStringLiteral("Late material"));
	QCOMPARE(result.resolution, QStringLiteral("1920x1080"));
	QVERIFY2(bytes < 64 * 1024, qPrintable(QString::number(bytes)));
}

void TestMxfParser::dynamic_primer_tags_are_resolved_by_property()
{
	QTemporaryDir temp;
	QByteArray entries;
	entries += u16be(0x9001) + ul("060e2b34010101010401050202000000"); // width
	entries += u16be(0x9002) + ul("060e2b34010101010401050201000000"); // height
	entries += u16be(0x3203) + QByteArray(16, 'x'); // same numeric tag now means something else
	const QByteArray primer = klv(ul("060e2b34020501010d01020101050100"), u32be(3) + u32be(18) + entries);
	const QByteArray descriptor = objectSet(0x28, 'd', localProperty(0x9001, u32be(1920)) +
		localProperty(0x9002, u32be(1080)) + localProperty(0x3203, u32be(9999)));
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("primer.mxf"), partitionPack() + primer + descriptor));
	QVERIFY(result.valid);
	QCOMPARE(result.resolution, QStringLiteral("1920x1080"));
}

void TestMxfParser::usage_requires_unambiguous_master_evidence_data()
{
	QTest::addColumn<int>("code");
	QTest::addColumn<QByteArray>("standard");
	QTest::addColumn<bool>("known");
	QTest::addColumn<bool>("precompute");
	const QByteArray lower = ul("060e2b34040101010d01010201010800");
	const QByteArray adjusted = ul("060e2b34040101010d01010201010600");
	QTest::newRow("ordinary-material-no-usage") << -1 << QByteArray() << true << false;
	QTest::newRow("lowerlevel-alone-is-ambiguous") << -1 << lower << false << false;
	QTest::newRow("private-precompute") << 1 << QByteArray() << true << true;
	QTest::newRow("precompute-corroborated") << 1 << lower << true << true;
	QTest::newRow("precompute-swapped-standard") << 1 << (lower.mid(8) + lower.left(8)) << true << true;
	QTest::newRow("master-explicit") << 7 << QByteArray() << true << false;
	QTest::newRow("master-adjusted") << 7 << adjusted << true << false;
	QTest::newRow("master-conflicts-with-lowerlevel") << 7 << lower << false << false;
	QTest::newRow("precompute-conflicts-with-adjusted") << 1 << adjusted << false << false;
	QTest::newRow("precompute-unknown-standard") << 1 << QByteArray(16, 'x') << false << false;
	QTest::newRow("unknown-standard") << -1 << QByteArray(16, 'x') << false << false;
	for (int code : {0, 2, 3, 4, 5, 6, 8, 9, 10, 99})
		QTest::newRow(qPrintable(QStringLiteral("other-code-%1").arg(code)))
			<< code << QByteArray() << false << false;
	// The installed binary maps group4 and motion6 to this SAME standard UID.
	QTest::newRow("group-is-not-precompute") << 4 << lower << false << false;
	QTest::newRow("motion-is-not-precompute") << 6 << lower << false << false;
}

void TestMxfParser::usage_requires_unambiguous_master_evidence()
{
	QFETCH(int, code);
	QFETCH(QByteArray, standard);
	QFETCH(bool, known);
	QFETCH(bool, precompute);
	QTemporaryDir temp;
	const QByteArray primer = klv(ul("060e2b34020501010d01020101050100"),
		u32be(1) + u32be(18) + u16be(0x9107) + ul(AvidUsage::kPrivateMxfPropertyHex));
	QByteArray fields = localProperty(0x4401, QByteArray(32, 'm'));
	if (code >= 0) fields += localProperty(0x9107, u32be(quint32(code)));
	if (!standard.isEmpty()) fields += localProperty(0x4408, standard);
	// A source/file code must never override the material/master verdict.
	const QByteArray source = objectSet(0x37, 'f', localProperty(0x4401, QByteArray(32, 'f')) +
		localProperty(0x9107, u32be(9)) + localProperty(0x4408, ul("060e2b34040101010d01010201010800")));
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("usage.mxf"),
		partitionPack() + primer + source + objectSet(0x36, 'm', fields) + cdciSet(1920, 1080, 25)));
	QVERIFY(result.valid);
	QCOMPARE(result.umid, MobId::format(QByteArray(32, 'm')));
	QVERIFY(result.hasMaterialPackage); // The master identity survives an unknown usage verdict.
	QCOMPARE(result.classificationKnown, known);
	QCOMPARE(result.isPrecompute, precompute);
}

void TestMxfParser::private_usage_requires_primer_and_exact_integer()
{
	QTemporaryDir temp;
	const auto material = [](quint16 tag, const QByteArray &value) {
		return objectSet(0x36, 'm', localProperty(0x4401, QByteArray(32, 'm')) +
			localProperty(0x4408, ul("060e2b34040101010d01010201010800")) + localProperty(tag, value));
	};
	for (quint16 tag : {quint16(0xfffa), AvidUsage::kPrivateMxfTag})
	{
		const auto unregistered = MxfParser::parseHeader(writeMxf(temp.filePath("unregistered.mxf"),
			partitionPack() + material(tag, u32be(1)) + cdciSet(1920, 1080, 25)));
		QVERIFY(unregistered.valid);
		QVERIFY(!unregistered.classificationKnown);
		QVERIFY(!unregistered.isPrecompute);
	}
	const QByteArray primer = klv(ul("060e2b34020501010d01020101050100"),
		u32be(1) + u32be(18) + u16be(0x9009) + ul(AvidUsage::kPrivateMxfPropertyHex));
	const auto negative = MxfParser::parseHeader(writeMxf(temp.filePath("negative.mxf"),
		partitionPack() + primer + objectSet(0x36, 'm', localProperty(0x4401, QByteArray(32, 'm')) +
			localProperty(0x9009, u32be(0xffffffffu))) + cdciSet(1920, 1080, 25)));
	QVERIFY(negative.valid);
	QVERIFY(!negative.classificationKnown); // Explicit -1 is not an absent property.
	QVERIFY(!negative.isPrecompute);
	for (const QByteArray &value : {QByteArray(), QByteArray::fromHex("0001"), QByteArray::fromHex("0000000000000001")})
	{
		const auto malformed = MxfParser::parseHeader(writeMxf(temp.filePath("bad-width.mxf"),
			partitionPack() + primer + material(0x9009, value) + cdciSet(1920, 1080, 25)));
		QCOMPARE(malformed.headerStatus, MxfMetadata::HeaderStatus::Malformed);
		QVERIFY(!malformed.hasMaterialPackage);
		QVERIFY(!malformed.classificationKnown);
	}
}

void TestMxfParser::owning_package_selects_descriptor()
{
	QTemporaryDir temp;
	const QByteArray fileId(32, 'f'), masterId(32, 'm');
	const auto descriptor = [](char id, quint32 width, quint32 rate) {
		return objectSet(0x28, id, localProperty(0x3203, u32be(width)) + localProperty(0x3202, u32be(1080)) +
			localProperty(0x3001, u32be(rate) + u32be(1)));
	};
	QByteArray content = partitionPack();
	content += objectSet(0x36, 'm', localProperty(0x4401, masterId) + localProperty(0x4402, utf16be("Selected master")) + localProperty(0x4403, references('t')));
	content += objectSet(0x3b, 't', localProperty(0x4803, QByteArray(16, 's')) + localProperty(0x4b01, u32be(25) + u32be(1)));
	content += objectSet(0x0f, 's', localProperty(0x1001, u32be(2) + u32be(16) + QByteArray(16, 'c') + QByteArray(16, 'C')) + localProperty(0x0202, u32be(250)));
	content += objectSet(0x11, 'c', localProperty(0x1101, fileId) + localProperty(0x0202, u32be(125)));
	content += objectSet(0x11, 'C', localProperty(0x1101, fileId) + localProperty(0x0202, u32be(125)));
	content += objectSet(0x37, 'f', localProperty(0x4401, fileId) + localProperty(0x4701, QByteArray(16, 'd')));
	content += descriptor('d', 1920, 25);
	content += objectSet(0x37, 'u', localProperty(0x4401, QByteArray(32, 'u')) + localProperty(0x4701, QByteArray(16, 'x')));
	content += descriptor('x', 9999, 60); // unrelated descriptor placed last
	content += objectSet(0x23, 'e', localProperty(0x2701, fileId));
	content += objectSet(0x36, 'a', localProperty(0x4401, QByteArray(32, 'a')) + localProperty(0x4402, utf16be("Other master")) + localProperty(0x4403, references('t')));
	content += objectSet(0x2f, 'p', localProperty(0x3b08, QByteArray(16, 'm')));
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("graph.mxf"), content));
	QVERIFY(result.valid);
	QCOMPARE(result.fileMobId, MobId::format(fileId));
	QCOMPARE(result.umid, MobId::format(masterId));
	QCOMPARE(result.clipName, QStringLiteral("Selected master"));
	QCOMPARE(result.resolution, QStringLiteral("1920x1080"));
	QCOMPARE(result.fps, QStringLiteral("25"));
	QCOMPARE(result.durationFrames, qint64(250));
	QVERIFY(result.classificationKnown);
}

void TestMxfParser::split_master_uses_individual_file_duration()
{
	QTemporaryDir temp;
	// One master combines 19 and 325 frames from different files. Each
	// file's own Sequence/ContainerDuration describes only its portion.
	for (const bool audio : {false, true})
		for (const bool fileTrack : {false, true})
		for (const bool descriptorDuration : {false, true})
		{
			if (!fileTrack && !descriptorDuration) continue;
			const quint32 rate = audio ? 48000 : 25;
			const quint32 units = audio ? 1920 : 1;
			const QByteArray fileId(32, 'f');
			QByteArray content = partitionPack();
			content += objectSet(0x36, 'm', localProperty(0x4401, QByteArray(32, 'm')) + localProperty(0x4403, references('t')));
			content += objectSet(0x3b, 't', localProperty(0x4803, QByteArray(16, 's')) + localProperty(0x4b01, u32be(rate) + u32be(1)));
			content += objectSet(0x0f, 's', localProperty(0x0202, u32be(344 * units)) +
				localProperty(0x1001, u32be(2) + u32be(16) + QByteArray(16, 'c') + QByteArray(16, 'C')));
			content += objectSet(0x11, 'c', localProperty(0x1101, fileId) + localProperty(0x0202, u32be(19 * units)));
			content += objectSet(0x11, 'C', localProperty(0x1101, QByteArray(32, 'g')) + localProperty(0x0202, u32be(325 * units)));
			content += objectSet(0x37, 'f', localProperty(0x4401, fileId) + localProperty(0x4701, QByteArray(16, 'd')) +
				(fileTrack ? localProperty(0x4403, references('v')) : QByteArray{}));
			QByteArray descriptor = localProperty(0x3001, u32be(rate) + u32be(1));
			if (descriptorDuration) descriptor += localProperty(0x3002, u32be(19 * units));
			if (audio) descriptor += localProperty(0x3d03, u32be(48000) + u32be(1)) + localProperty(0x3d07, u32be(1));
			else descriptor += localProperty(0x3203, u32be(1920)) + localProperty(0x3202, u32be(1080));
			content += objectSet(audio ? 0x48 : 0x28, 'd', descriptor);
			if (fileTrack)
			{
				content += objectSet(0x3b, 'v', localProperty(0x4803, QByteArray(16, 'q')) + localProperty(0x4b01, u32be(rate) + u32be(1)));
				// A longer hold/track never overrides the stored essence length.
				content += objectSet(0x0f, 'q', localProperty(0x0202, u32be((descriptorDuration ? 2880 : 19) * units)));
			}
			// A source/timecode track supplies the frame rate for sample units.
			content += objectSet(0x37, 'g', localProperty(0x4401, QByteArray(32, 'g')) + localProperty(0x4403, references('r')));
			content += objectSet(0x3b, 'r', localProperty(0x4b01, u32be(25) + u32be(1)));
			content += objectSet(0x23, 'e', localProperty(0x2701, fileId));
			const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("split.mxf"), content));
			QVERIFY(result.valid);
			QCOMPARE(result.durationFrames, qint64(19));
			QCOMPARE(result.timecodeBase, 25);
		}
}

void TestMxfParser::malformed_local_property_invalidates_header()
{
	QTemporaryDir temp;
	const QByteArray bad = objectSet(0x36, 'm', u16be(0x4401) + u16be(32) + QByteArray(2, 'x'));
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("broken.mxf"), partitionPack() + cdciSet(1920, 1080) + bad));
	QVERIFY(!result.valid);
	QVERIFY(!result.classificationKnown);
	QCOMPARE(result.headerStatus, MxfMetadata::HeaderStatus::Malformed);
	const QByteArray badUsage = objectSet(0x36, 'm', localProperty(0x4401, QByteArray(32, 'm')) + localProperty(0x4408, QByteArray(1, '\0')));
	const auto usage = MxfParser::parseHeader(writeMxf(temp.filePath("usage.mxf"), partitionPack() + cdciSet(1920, 1080) + badUsage));
	QVERIFY(!usage.classificationKnown);
	QVERIFY(!usage.valid);
	const QByteArray fill = klv(ul("060e2b34010101010301021001000000"), QByteArray(100, '\0'));
	const auto shortFill = MxfParser::parseHeader(writeMxf(temp.filePath("fill.mxf"), partitionPack() + cdciSet(1920, 1080) + fill.left(fill.size() - 50)));
	QCOMPARE(shortFill.headerStatus, MxfMetadata::HeaderStatus::Incomplete);
	QVERIFY(!shortFill.valid);
	const QByteArray descriptor = cdciSet(1920, 1080);
	QByteArray pack(88, '\0');
	// A KLV crossing HeaderByteCount is not a complete metadata region.
	qToBigEndian<quint64>(quint64(descriptor.size() - 1), pack.data() + 32);
	const auto crossing = MxfParser::parseHeader(writeMxf(temp.filePath("crossing.mxf"),
		klv(ul("060e2b34020501010d01020101020400"), pack) + descriptor));
	QCOMPARE(crossing.headerStatus, MxfMetadata::HeaderStatus::Malformed);
	QVERIFY(!crossing.valid);
	// The file may be long enough while essence starts too early for the
	// declared metadata extent. Do not certify that header as complete.
	qToBigEndian<quint64>(quint64(descriptor.size() + 50), pack.data() + 32);
	const auto earlyEssence = MxfParser::parseHeader(writeMxf(temp.filePath("early.mxf"),
		klv(ul("060e2b34020501010d01020101020400"), pack) + descriptor +
		klv(ul("060e2b34010201010d01030115010501"), QByteArray(100, '\0'))));
	QCOMPARE(earlyEssence.headerStatus, MxfMetadata::HeaderStatus::Malformed);
	QVERIFY(!earlyEssence.valid);
}

void TestMxfParser::finalise_is_idempotent_and_does_not_guess()
{
	MxfMetadata picture;
	picture.width = 1920;
	picture.height = 540;
	picture.frameLayout = 1;
	MxfParser::finalise(picture);
	MxfParser::finalise(picture);
	QCOMPARE(picture.height, 1080);
	QVERIFY(picture.codec.isEmpty());
	MxfMetadata corrupt;
	corrupt.width = 1920;
	corrupt.height = std::numeric_limits<int>::min();
	corrupt.frameLayout = 1;
	MxfParser::finalise(corrupt);
	QCOMPARE(corrupt.height, 0);
	QVERIFY(!corrupt.valid);
	MxfMetadata sound;
	sound.isAudio = true;
	sound.sampleRate = 48000;
	MxfParser::finalise(sound);
	QVERIFY(sound.valid);
	QVERIFY(sound.codec.isEmpty());
	sound.pcmDescriptor = true;
	MxfParser::finalise(sound);
	QCOMPARE(sound.codec, QString::fromLatin1(kPcmAudioName));
	const QByteArray sq = ul("060E2B34040101010D01030102060101");
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, {}), QStringLiteral("Avid DNx SQ"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, "50"), QStringLiteral("Avid DNx SQ (DNxHD 240)"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(sq, "59.94"), QStringLiteral("Avid DNx SQ (DNxHD 290)"));
	QVERIFY(!MxfParser::codecFromEssenceLabel(ul("060e2b34040101010d99111111111111"), {}).startsWith("Avid"));
}

void TestMxfParser::avid_alpha_requires_positive_container_and_layout_data()
{
	QTest::addColumn<QByteArray>("containers");
	QTest::addColumn<int>("descriptorType");
	QTest::addColumn<QByteArray>("layout");
	QTest::addColumn<QByteArray>("codingField");
	QTest::addColumn<bool>("alpha");
	const QByteArray unc = ul("060e2b34040101010e04030102080100");
	const QByteArray a8 = ul("41080000300030003000300030003000");
	const QByteArray other = ul("060e2b34040101010e04030102010100");
	QTest::newRow("Avid-alpha-padding") << unc << 0x29 << a8 << QByteArray{} << true;
	QTest::newRow("zero-padding") << unc << 0x29 << (QByteArray::fromHex("4108") + QByteArray(14, '\0')) << QByteArray{} << true;
	QTest::newRow("no-container") << QByteArray{} << 0x29 << a8 << QByteArray{} << false;
	QTest::newRow("different-container") << other << 0x29 << a8 << QByteArray{} << false;
	QTest::newRow("ambiguous-containers") << (unc + other) << 0x29 << a8 << QByteArray{} << false;
	QTest::newRow("wrong-class") << unc << 0x28 << a8 << QByteArray{} << false;
	QTest::newRow("missing-layout") << unc << 0x29 << QByteArray{} << QByteArray{} << false;
	QTest::newRow("short-layout") << unc << 0x29 << a8.left(2) << QByteArray{} << false;
	QTest::newRow("different-depth") << unc << 0x29 << ul("41100000300030003000300030003000") << QByteArray{} << false;
	QTest::newRow("RGB-not-alpha") << unc << 0x29 << ul("52084708420800000000000000000000") << QByteArray{} << false;
	QTest::newRow("alpha-and-color") << unc << 0x29 << ul("41085208000000000000000000000000") << QByteArray{} << false;
	QTest::newRow("unknown-explicit-coding") << unc << 0x29 << a8 << localProperty(0x3201, QByteArray(16, 'x')) << false;
	QTest::newRow("empty-explicit-coding") << unc << 0x29 << a8 << localProperty(0x3201, {}) << false;
}

void TestMxfParser::avid_alpha_requires_positive_container_and_layout()
{
	QFETCH(QByteArray, containers);
	QFETCH(int, descriptorType);
	QFETCH(QByteArray, layout);
	QFETCH(QByteArray, codingField);
	QFETCH(bool, alpha);
	QTemporaryDir temp;
	QByteArray pack(80, '\0');
	pack += u32be(quint32(containers.size() / 16)) + u32be(16) + containers;
	const QByteArray fields = localProperty(0x3203, u32be(1920)) + localProperty(0x3202, u32be(1080)) +
		localProperty(0x3001, u32be(24) + u32be(1)) + localProperty(0x3401, layout) + codingField;
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("alpha.mxf"),
		klv(ul("060e2b34020501010d01020101020400"), pack) + objectSet(quint8(descriptorType), 'd', fields)));
	QVERIFY(result.valid);
	QCOMPARE(result.headerStatus, MxfMetadata::HeaderStatus::Complete);
	QCOMPARE(result.codec == QStringLiteral("Uncompressed alpha"), alpha);
	if (alpha)
	{
		QCOMPARE(result.bitDepth, QStringLiteral("8-bit"));
		QCOMPARE(result.fps, QStringLiteral("24"));
	}
}

void TestMxfParser::avid_alpha_respects_descriptor_selection_and_primer()
{
	QTemporaryDir temp;
	const QByteArray pack = klv(ul("060e2b34020501010d01020101020400"),
		QByteArray(80, '\0') + u32be(1) + u32be(16) + ul("060e2b34040101010e04030102080100"));
	const QByteArray dimensions = localProperty(0x3203, u32be(1920)) + localProperty(0x3202, u32be(1080));
	const QByteArray alpha = objectSet(0x29, 'a', dimensions + localProperty(0x3401, ul("41080000300030003000300030003000")));
	const QByteArray sourceId(32, 'f');
	const QByteArray graph = objectSet(0x37, 'f', localProperty(0x4401, sourceId) + localProperty(0x4701, QByteArray(16, 'd'))) +
		objectSet(0x23, 'e', localProperty(0x2701, sourceId));
	// The unreferenced RGBA descriptor cannot name the selected CDCI essence.
	const auto selected = MxfParser::parseHeader(writeMxf(temp.filePath("selected.mxf"),
		pack + graph + alpha + objectSet(0x28, 'd', dimensions)));
	QVERIFY(selected.valid);
	QVERIFY(selected.codec.isEmpty());
	QVERIFY(!selected.rgbaDescriptor);
	// Two standalone descriptors have no unambiguous selection.
	const auto ambiguous = MxfParser::parseHeader(writeMxf(temp.filePath("ambiguous.mxf"),
		pack + alpha + objectSet(0x28, 'd', dimensions)));
	QVERIFY(!ambiguous.valid);
	QVERIFY(ambiguous.codec.isEmpty());
	// PixelLayout can use a dynamic tag; the primer establishes its identity.
	QByteArray entries;
	const struct { quint16 tag; const char *property; } mappings[] = {
		{0x3203, "060e2b34010101010401050202000000"},
		{0x3202, "060e2b34010101010401050201000000"},
		{0x9101, "060e2b34010101020401050306000000"},
	};
	for (const auto &mapping : mappings) entries += u16be(mapping.tag) + ul(mapping.property);
	const QByteArray primer = klv(ul("060e2b34020501010d01020101050100"), u32be(3) + u32be(18) + entries);
	const auto remapped = MxfParser::parseHeader(writeMxf(temp.filePath("remapped.mxf"), pack + primer +
		objectSet(0x29, 'd', dimensions + localProperty(0x9101, ul("41080000300030003000300030003000")))));
	QVERIFY(remapped.valid);
	QCOMPARE(remapped.codec, QStringLiteral("Uncompressed alpha"));
	QCOMPARE(remapped.bitDepth, QStringLiteral("8-bit"));
}

void TestMxfParser::partition_container_batch_is_bounded()
{
	QTemporaryDir temp;
	const struct { quint32 count; quint32 stride; int bytes; } malformed[] = {
		{1, 15, 16}, {2, 16, 16}, {0, 16, 16}, {0xffffffffU, 16, 16}, {0, 1, 0}
	};
	for (const auto &row : malformed)
	{
		const QByteArray pack = QByteArray(80, '\0') + u32be(row.count) + u32be(row.stride) + QByteArray(row.bytes, '\0');
		const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("malformed.mxf"),
			klv(ul("060e2b34020501010d01020101020400"), pack) + cdciSet(1920, 1080)));
		QVERIFY(!result.valid);
		QCOMPARE(result.headerStatus, MxfMetadata::HeaderStatus::Malformed);
		QVERIFY(result.codec.isEmpty());
	}
}

namespace
{
QByteArray projectAttribute(char instance, const QString &value)
{
	// AAF Indirect UTF-16BE string, referenced through the package's Avid
	// attribute list. This is a real tagged-value shape, not a parser seam.
	const QByteArray indirect = QByteArray(1, 'B') + ul("0110020000000000060e2b3401040101") + utf16be(value);
	return objectSet(0x3f, instance, localProperty(0x5001, utf16be(QStringLiteral("_PJ"))) +
		localProperty(0x5003, indirect));
}
QByteArray projectFileGraph(const QByteArray &fileAttributes = {}, const QByteArray &materialAttributes = {})
{
	const QByteArray fileId(32, 'f');
	return partitionPack() +
		objectSet(0x36, 'm', localProperty(0x4401, QByteArray(32, 'm')) + localProperty(0x4403, references('t')) + materialAttributes) +
		objectSet(0x3b, 't', localProperty(0x4803, QByteArray(16, 'c'))) +
		objectSet(0x11, 'c', localProperty(0x1101, fileId)) +
		objectSet(0x37, 'f', localProperty(0x4401, fileId) + localProperty(0x4701, QByteArray(16, 'd')) +
			localProperty(0x4403, references('r')) + fileAttributes) +
		objectSet(0x3b, 'r', localProperty(0x4803, QByteArray(16, 's'))) +
		objectSet(0x11, 's', localProperty(0x1101, QByteArray(32, 'o'))) +
		objectSet(0x37, 'o', localProperty(0x4401, QByteArray(32, 'o')) + localProperty(0xf001, references('a'))) +
		objectSet(0x28, 'd', localProperty(0x3203, u32be(1920)) + localProperty(0x3202, u32be(1080)) +
			localProperty(0x3001, u32be(25) + u32be(1))) +
		objectSet(0x23, 'e', localProperty(0x2701, fileId));
}
}

void TestMxfParser::project_prefers_owning_file_then_material_then_linked_source()
{
	QTemporaryDir temp;
	for (int tier = 0; tier < 3; ++tier)
	{
		const QByteArray own = tier == 0 ? localProperty(0xf001, references('b')) : QByteArray{};
		const QByteArray material = tier < 2 ? localProperty(0x4406, references('h')) : QByteArray{};
		QByteArray content = projectFileGraph(own, material);
		// The older imported source is deliberately serialized first, just as
		// in export row104: HAA_PROGRAMME_BUILD versus BLOCK_1729_PS_DG.
		content += projectAttribute('a', QStringLiteral("HAA_PROGRAMME_BUILD"));
		if (tier == 0) content += projectAttribute('b', QStringLiteral("BLOCK_1729_PS_DG"));
		if (tier < 2) content += projectAttribute('h', QStringLiteral("MATERIAL_PROJECT"));
		const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("project.mxf"), content));
		QVERIFY(result.valid);
		QCOMPARE(result.fileMobId, MobId::format(QByteArray(32, 'f')));
		QCOMPARE(result.resolution, QStringLiteral("1920x1080"));
		QCOMPARE(result.projectName, tier == 0 ? QStringLiteral("BLOCK_1729_PS_DG") :
			tier == 1 ? QStringLiteral("MATERIAL_PROJECT") : QStringLiteral("HAA_PROGRAMME_BUILD"));
	}
}

void TestMxfParser::project_recovers_unique_orphan_attribute()
{
	QTemporaryDir temp;
	const QByteArray content = projectFileGraph() +
		projectAttribute('u', QStringLiteral("RECOVERABLE_PROJECT")) +
		projectAttribute('v', QStringLiteral("RECOVERABLE_PROJECT"));
	const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("orphan.mxf"), content));
	QVERIFY(result.valid);
	QCOMPARE(result.projectName, QStringLiteral("RECOVERABLE_PROJECT"));
}

void TestMxfParser::project_leaves_conflicting_fallbacks_unknown()
{
	QTemporaryDir temp;
	// Neither unrelated project has a stronger claim; serialization order
	// must never settle it. A conflict within the owning attribute list must
	// not fall through to a convenient but older source project either.
	for (const bool ownConflict : {false, true})
		for (const bool reverse : {false, true})
		{
			const QByteArray own = ownConflict ? localProperty(0xf001,
				u32be(2) + u32be(16) + QByteArray(16, 'u') + QByteArray(16, 'v')) : QByteArray{};
			QByteArray content = projectFileGraph(own);
			const QByteArray first = projectAttribute('u', QStringLiteral("PROJECT_ONE"));
			const QByteArray second = projectAttribute('v', QStringLiteral("PROJECT_TWO"));
			content += reverse ? second + first : first + second;
			if (ownConflict) content += projectAttribute('a', QStringLiteral("OLDER_SOURCE"));
			const auto result = MxfParser::parseHeader(writeMxf(temp.filePath("ambiguous.mxf"), content));
			QVERIFY(result.valid);
			QVERIFY(result.projectName.isEmpty());
		}
}

QTEST_APPLESS_MAIN(TestMxfParser)
#include "tst_mxfparser.moc"
