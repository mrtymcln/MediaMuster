// BentoFile: the container primitive under msmMMOB.mdb. Label + TOC
// arithmetic, immediate vs offset values, first-in-file-wins on duplicate
// entries, the file's own property-name dictionary, and the typed readers —
// against builder-made containers and the three real MDB fixtures.
// The omf_* cases cover the OMF-era tail-first mode (open()/bytes()/
// mobIndex()) against the OMF fixtures; the two gates that differ by mode
// (continued entries, label major version) have cases of their own.

#include "bentofile.h"
#include "testbento.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	QByteArray readFile(const QString &path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}

	QString writeTemp(const QTemporaryDir &dir, const char *name, const QByteArray &data)
	{
		const QString path = dir.filePath(QString::fromLatin1(name));
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly))
			return {};
		f.write(data);
		return path;
	}

	/// One 20-byte mob-index row as Avid writes it: 12-byte UID (u32 prefix
	/// + 8 core bytes) | u32 object | u32 junk.
	QByteArray mobIndexRow(const QByteArray &core8, quint32 object, quint32 junk)
	{
		return BentoBuilder::le32(0x2a) + core8 + BentoBuilder::le32(object) + BentoBuilder::le32(junk);
	}

	constexpr qint64 kTailBudget = 64 * 1024; ///< open() must stay under this on any file.
} // namespace

class TestBentoFile : public QObject
{
	Q_OBJECT
private slots:
	void rejects_a_missing_or_garbled_label();
	void rejects_toc_arithmetic_that_does_not_close();
	void reads_immediate_and_offset_values();
	void out_of_range_value_reads_empty_not_a_crash();
	void duplicate_entries_first_in_file_wins();
	void property_names_come_from_the_file();
	void typed_readers();
	void real_fixtures_load_with_the_expected_shape();
	void continued_entry_reads_empty_in_load_mode_and_refuses_open();
	void label_version_gate_is_major_only_and_open_mode_only();
	void omf_open_matches_load_on_real_fixtures();
	void omf_open_reads_only_the_tail();
	void omf_open_fails_cleanly_on_non_bento_input();
	void omf_mob_index();
};

void TestBentoFile::rejects_a_missing_or_garbled_label()
{
	BentoFile b;
	QString why;
	QVERIFY(!b.load(QByteArray(), &why));
	QVERIFY(!why.isEmpty());
	QVERIFY(!b.load(QByteArray(100, 'x'), &why));
	QVERIFY(why.contains(QStringLiteral("label")));

	// Right magic, wrong byte-order tag.
	QByteArray l = BentoBuilder::label(0, 0);
	l[10] = 'M';
	l[11] = 'M';
	QVERIFY(!b.load(l, &why));
	QCOMPARE(b.entryCount(), 0);
}

void TestBentoFile::rejects_toc_arithmetic_that_does_not_close()
{
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.setString(o, "OMFI:CPNT:Name", "x");

	BentoFile ok;
	QVERIFY(ok.load(w.build()));

	// Stray bytes between the TOC and the label: off + len != size - 24.
	BentoFile bad;
	QString why;
	QVERIFY(!bad.load(w.build(QByteArray(3, '\0')), &why));
	QVERIFY2(why.contains(QStringLiteral("arithmetic")), qPrintable(why));

	// A TOC whose length is not a multiple of 24 — a 20-byte trailer with a
	// label that still closes the arithmetic is the only way to produce one.
	QByteArray raw = w.build();
	raw.chop(24);
	const quint32 tocOff = 0; // lie: the whole body is "TOC"
	raw += BentoBuilder::label(tocOff, quint32(raw.size()) + 0);
	// raw.size() is unlikely to be a multiple of 24 here; if it is, pad one.
	if ((raw.size() - 24) % 24 == 0)
	{
		raw.chop(24);
		raw += '\0';
		raw += BentoBuilder::label(0, quint32(raw.size()));
	}
	BentoFile bad2;
	QVERIFY(!bad2.load(raw, &why));
}

void TestBentoFile::reads_immediate_and_offset_values()
{
	BentoBuilder w;
	const quint32 o = w.addObject("CDCI");
	w.setU32(o, "OMFI:DIDD:StoredWidth", 1920);
	w.setU16(o, "OMFI:DIDD:FrameLayout", 3);
	w.setString(o, "OMFI:CPNT:Name", "Clip");
	w.setImmediate(o, "OMFI:DIDD:DIDCompressMethod", QByteArray("AVHD", 4));

	BentoFile b;
	QVERIFY(b.load(w.build()));
	QCOMPARE(b.objectClass(o), QByteArray("CDCI"));
	QCOMPARE(BentoFile::uint(b.value(o, b.propertyId("OMFI:DIDD:StoredWidth"))), 1920u);
	QCOMPARE(b.value(o, b.propertyId("OMFI:DIDD:FrameLayout")).size(), qsizetype(2));
	QCOMPARE(BentoFile::uint(b.value(o, b.propertyId("OMFI:DIDD:FrameLayout"))), 3u);
	// Immediate bytes come back in file order, not byte-swapped.
	QCOMPARE(b.value(o, b.propertyId("OMFI:DIDD:DIDCompressMethod")).toByteArray(), QByteArray("AVHD"));
	QCOMPARE(BentoFile::string(b.value(o, b.propertyId("OMFI:CPNT:Name"))), QStringLiteral("Clip"));
	// Unknown property / unknown object read empty.
	QCOMPARE(b.propertyId("OMFI:Nope"), -1);
	QVERIFY(b.value(o, b.propertyId("OMFI:Nope")).isEmpty());
	QVERIFY(b.value(o + 99, b.propertyId("OMFI:CPNT:Name")).isEmpty());
}

void TestBentoFile::out_of_range_value_reads_empty_not_a_crash()
{
	// Build a valid file, then point one entry's offset past the value area —
	// the shape of Avid's "object 1 describes the whole file" entries.
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.setString(o, "OMFI:CPNT:Name", "Clip");
	QByteArray raw = w.build();

	BentoFile probe;
	QVERIFY(probe.load(raw));
	const int nameProp = probe.propertyId("OMFI:CPNT:Name");
	// Find that entry in the TOC and rewrite its offset to an absurd value.
	const quint32 tocOff = probe.tocOffset();
	bool patched = false;
	for (qint64 pos = tocOff; pos + 24 <= raw.size() - 24; pos += 24)
	{
		const auto obj = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData() + pos));
		const auto prop = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(raw.constData() + pos + 4));
		if (obj == o && prop == quint32(nameProp))
		{
			qToLittleEndian<quint32>(0x7fffff00u, reinterpret_cast<uchar *>(raw.data() + pos + 12));
			qToLittleEndian<quint32>(0x7fffff00u, reinterpret_cast<uchar *>(raw.data() + pos + 16));
			patched = true;
		}
	}
	QVERIFY(patched);
	BentoFile b;
	QVERIFY(b.load(raw));
	QVERIFY(b.value(o, nameProp).isEmpty());
}

void TestBentoFile::duplicate_entries_first_in_file_wins()
{
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.setString(o, "OMFI:CPNT:Name", "first");
	w.setString(o, "OMFI:CPNT:Name", "second");
	BentoFile b;
	QVERIFY(b.load(w.build()));
	QCOMPARE(BentoFile::string(b.value(o, b.propertyId("OMFI:CPNT:Name"))), QStringLiteral("first"));
	QCOMPARE(b.objectsWithProperty(b.propertyId("OMFI:CPNT:Name")), QVector<quint32>{o});
}

void TestBentoFile::property_names_come_from_the_file()
{
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.setString(o, "OMFI:CPNT:Name", "n");
	w.setU32(o, "OMFI:MOBJ:UsageCode", 7);
	BentoFile b;
	QVERIFY(b.load(w.build()));
	QVERIFY(b.propertyId("OMFI:CPNT:Name") > 0);
	QVERIFY(b.propertyId("OMFI:MOBJ:UsageCode") > 0);
	QVERIFY(b.propertyId("OMFI:ObjID") > 0);
	QCOMPARE(b.propertyNameCount(), 3);
	QCOMPARE(b.objectsWithProperty(b.propertyId("OMFI:MOBJ:UsageCode")), QVector<quint32>{o});
}

void TestBentoFile::typed_readers()
{
	BentoBuilder w;
	const quint32 a = w.addObject("MOBJ");
	const quint32 t = w.addObject("TRAK");
	w.setRational(a, "OMFI:CPNT:EditRate", 2997, 100);
	w.setHandle(a, "OMFI:MOBJ:PhysicalMedia", t);
	w.setHandles(a, "OMFI:TRKG:Tracks", {t, t + 1});
	w.setString(a, "OMFI:CPNT:Name", QByteArray("zT_\xa7t", 5)); // MacRoman ß
	w.setString(a, "OMFI:MCBR:MC:binNameUTF8", QByteArray("zT_\xc3\x9ft", 6));
	const QByteArray mob = QByteArray::fromHex("060a2b340101010501010f1013000000"
											   "4a507dea741106907a361e6a605d3613");
	w.set(a, "OMFI:MOBJ:MobID", mob);

	BentoFile b;
	QVERIFY(b.load(w.build()));
	qint32 num = 0, den = 0;
	QVERIFY(BentoFile::rational(b.value(a, b.propertyId("OMFI:CPNT:EditRate")), num, den));
	QCOMPARE(num, 2997);
	QCOMPARE(den, 100);
	QCOMPARE(BentoFile::handle(b.value(a, b.propertyId("OMFI:MOBJ:PhysicalMedia"))), t);
	QCOMPARE(BentoFile::handles(b.value(a, b.propertyId("OMFI:TRKG:Tracks"))), (QVector<quint32>{t, t + 1}));
	QCOMPARE(BentoFile::string(b.value(a, b.propertyId("OMFI:CPNT:Name"))), QString::fromUtf8("zT_ßt"));
	QCOMPARE(BentoFile::utf8String(b.value(a, b.propertyId("OMFI:MCBR:MC:binNameUTF8"))),
			 QString::fromUtf8("zT_ßt"));
	QCOMPARE(BentoFile::mobIdHex(b.value(a, b.propertyId("OMFI:MOBJ:MobID"))),
			 QStringLiteral("060a2b3401010105.01010f1013000000.4a507dea74110690.7a361e6a605d3613"));
	// Malformed shapes read as nothing rather than something.
	QCOMPARE(BentoFile::handle(QByteArrayView("abc")), 0u);
	QVERIFY(BentoFile::handles(QByteArrayView("\x05\x00zz")).isEmpty());
	QVERIFY(!BentoFile::rational(QByteArrayView("1234"), num, den));
	QVERIFY(BentoFile::mobIdHex(QByteArrayView("short")).isEmpty());
}

void TestBentoFile::real_fixtures_load_with_the_expected_shape()
{
	struct Fx
	{
		const char *path;
		int entries;
		int minNames;
		bool omfEra; ///< OMF-era: MobIDs are 12-byte omfi:UIDs (MC 2026 adds 32-byte UMIDs).
	};
	const Fx fixtures[] = {
		{FIXTURES_DIR "/msmMMOB.mdb", 1432, 700, false},
		{FIXTURES_DIR "/msmMMOB_macroman.mdb", -1, 700, false},
		{FIXTURES_DIR "/corpus_headers/msmMMOB_round3.mdb", 221406, 700, false},
		{FIXTURES_DIR "/omf/avid_supporting/msmMMOB.mdb", 20491, 150, true},
		{FIXTURES_DIR "/omf/mc2026_audio/msmMMOB.mdb", 1368, 700, true},
	};
	for (const Fx &fx : fixtures)
	{
		BentoFile b;
		QString why;
		QVERIFY2(b.load(readFile(QString::fromLatin1(fx.path)), &why), qPrintable(why));
		if (fx.entries > 0)
			QCOMPARE(b.entryCount(), fx.entries);
		QVERIFY2(b.propertyNameCount() >= fx.minNames, qPrintable(QString::number(b.propertyNameCount())));
		const int mobIdProp = b.propertyId("OMFI:MOBJ:MobID");
		QVERIFY(mobIdProp > 0);
		const QVector<quint32> mobs = b.objectsWithProperty(mobIdProp);
		QVERIFY(!mobs.isEmpty());
		// Every MOBJ object's class reads as MOBJ, and its MobID starts 06 0a 2b 34 —
		// or, in an OMF-era database, is 12 or 32 bytes wide.
		for (quint32 o : mobs)
		{
			QCOMPARE(b.objectClass(o), QByteArray("MOBJ"));
			const QByteArray mob = b.value(o, mobIdProp).toByteArray();
			if (fx.omfEra)
				QVERIFY2(mob.size() == 12 || mob.size() == 32, qPrintable(QString::number(mob.size())));
			else
				QVERIFY(mob.startsWith(QByteArray::fromHex("060a2b34")));
		}
	}
	// The round-3 database: 1,392 MOBJ objects carry a MobID (1,380 distinct).
	BentoFile r3;
	QVERIFY(r3.load(readFile(QStringLiteral(FIXTURES_DIR "/corpus_headers/msmMMOB_round3.mdb"))));
	QCOMPARE(r3.objectsWithProperty(r3.propertyId("OMFI:MOBJ:MobID")).size(), 1392);
}

void TestBentoFile::continued_entry_reads_empty_in_load_mode_and_refuses_open()
{
	// A "continued" value (TOC flag bit 1) spans several entries; reading
	// only the first would be a silent truncation. Load mode — the MXF-era
	// MDB path — keeps the database readable and reads THAT value as empty,
	// every other value as before (one flag-2 entry must not cost a whole
	// folder its bin/source/import facts); the tail-first mode refuses the
	// file, since it cannot assemble the parts.
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.set(o, "OMFI:CPNT:Name", QByteArray("first half\0", 11), 2);
	w.setU32(o, "OMFI:MOBJ:UsageCode", 7);
	const quint32 other = w.addObject("MOBJ");
	w.setString(other, "OMFI:CPNT:Name", "intact");
	const QByteArray raw = w.build();

	BentoFile b;
	QString why;
	QVERIFY2(b.load(raw, &why), qPrintable(why));
	QCOMPARE(b.entryCount(), 8); // two ObjIDs, the continued name, the usage, the intact name, three dictionary entries
	const int nameProp = b.propertyId("OMFI:CPNT:Name");
	QVERIFY(nameProp > 0);
	QVERIFY(b.value(o, nameProp).isEmpty()); // never "first half"
	QVERIFY(b.bytes(o, nameProp).isEmpty());
	QCOMPARE(BentoFile::uint(b.value(o, b.propertyId("OMFI:MOBJ:UsageCode"))), 7u);
	QCOMPARE(BentoFile::string(b.value(other, nameProp)), QStringLiteral("intact"));
	QCOMPARE(b.objectClass(o), QByteArray("MOBJ"));
	int continued = 0;
	for (const BentoFile::Entry &e : b.entries())
		continued += e.continued ? 1 : 0;
	QCOMPARE(continued, 1);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoFile opened;
	why.clear();
	QVERIFY(!opened.open(writeTemp(tmp, "cont.bento", raw), &why));
	QVERIFY2(why.contains(QStringLiteral("continued")), qPrintable(why));
	QCOMPARE(opened.entryCount(), 0);
}

void TestBentoFile::label_version_gate_is_major_only_and_open_mode_only()
{
	// The label's version is two u16s (major, minor). A minor bump keeps the
	// TOC layout and is accepted everywhere; a major bump is refused by the
	// tail-first mode only — load() never checked it before and still
	// reads the file (the arithmetic gates are what it relies on).
	BentoBuilder w;
	const quint32 o = w.addObject("MOBJ");
	w.setString(o, "OMFI:CPNT:Name", "x");
	QVERIFY(BentoFile().load(w.build({}, 1)));

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString why;

	// Major 1, minor 1: u32 65537 writes as 01 00 01 00.
	const QByteArray minorBump = w.build({}, 0x00010001u);
	BentoFile m;
	QVERIFY2(m.load(minorBump, &why), qPrintable(why));
	QCOMPARE(BentoFile::string(m.value(o, m.propertyId("OMFI:CPNT:Name"))), QStringLiteral("x"));
	QVERIFY2(m.open(writeTemp(tmp, "v1_1.bento", minorBump), &why), qPrintable(why));
	QCOMPARE(BentoFile::string(m.bytes(o, m.propertyId("OMFI:CPNT:Name"))), QStringLiteral("x"));

	// Major 2: load() reads it (with a warning), open() refuses it by name.
	const QByteArray majorBump = w.build({}, 2);
	BentoFile b;
	QVERIFY2(b.load(majorBump, &why), qPrintable(why));
	QCOMPARE(BentoFile::string(b.value(o, b.propertyId("OMFI:CPNT:Name"))), QStringLiteral("x"));
	why.clear();
	QVERIFY(!b.open(writeTemp(tmp, "v2.bento", majorBump), &why));
	QVERIFY2(why.contains(QStringLiteral("version 2")), qPrintable(why));
	QCOMPARE(b.entryCount(), 0);
}

void TestBentoFile::omf_open_matches_load_on_real_fixtures()
{
	// The tail-first mode must see exactly what the whole-buffer mode sees:
	// same entries, same names, same bytes for every (object, property).
	const char *paths[] = {
		FIXTURES_DIR "/msmMMOB.mdb",
		FIXTURES_DIR "/omf/avid_supporting/BLACK_720x243x2_JFIF35.omf",
		FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav",
	};
	for (const char *p : paths)
	{
		const QString path = QString::fromLatin1(p);
		BentoFile loaded, opened;
		QString why;
		QVERIFY2(loaded.load(readFile(path), &why), qPrintable(why));
		QVERIFY2(opened.open(path, &why), qPrintable(why));

		QCOMPARE(opened.entryCount(), loaded.entryCount());
		QCOMPARE(opened.propertyNameCount(), loaded.propertyNameCount());
		QCOMPARE(opened.tocOffset(), loaded.tocOffset());
		QVERIFY(opened.entryCount() > 0);
		QVERIFY(opened.propertyNameCount() > 0);

		const int nameProp = 24;
		for (int i = 0; i < loaded.entryCount(); ++i)
		{
			const BentoFile::Entry &a = loaded.entries()[i];
			const BentoFile::Entry &b = opened.entries()[i];
			QCOMPARE(b.object, a.object);
			QCOMPARE(b.property, a.property);
			QCOMPARE(b.type, a.type);
			QCOMPARE(b.value, a.value);
			QCOMPARE(b.length, a.length);
			QCOMPARE(b.tocPos, a.tocPos);
			QCOMPARE(b.immediate, a.immediate);
			// Same bytes in both modes — the dictionary span and the seek+read
			// path both get exercised here, as does the shared 1 MiB cap.
			QCOMPARE(opened.bytes(a.object, int(a.property)), loaded.bytes(a.object, int(a.property)));
			QCOMPARE(opened.objectClass(a.object), loaded.objectClass(a.object));
			if (a.property == quint32(nameProp) && !a.immediate)
			{
				const QByteArray name = loaded.bytes(a.object, nameProp);
				QCOMPARE(opened.propertyId(name.left(name.indexOf('\0'))),
						 loaded.propertyId(name.left(name.indexOf('\0'))));
			}
		}
		// load-mode bytes() is the same thing value() returns, copied.
		const int mobIdProp = loaded.propertyId("OMFI:MOBJ:MobID");
		QVERIFY(mobIdProp > 0);
		for (quint32 o : loaded.objectsWithProperty(mobIdProp))
		{
			QCOMPARE(loaded.bytes(o, mobIdProp), loaded.value(o, mobIdProp).toByteArray());
			QCOMPARE(opened.bytes(o, mobIdProp), loaded.value(o, mobIdProp).toByteArray());
		}
	}
}

void TestBentoFile::omf_open_reads_only_the_tail()
{
	// 8.7 MB of audio: open() reads the label, the TOC and the dictionary
	// span — nothing else — and the essence entry can never be materialised.
	BentoFile wav;
	QString why;
	QVERIFY2(wav.open(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav"), &why),
			 qPrintable(why));
	QVERIFY2(wav.bytesRead() < kTailBudget, qPrintable(QString::number(wav.bytesRead())));
	QCOMPARE(wav.entryCount(), 374);
	QCOMPARE(wav.propertyNameCount(), 70);

	const int essenceProp = wav.propertyId("OMFI:WAVE:Data");
	QVERIFY(essenceProp > 0);
	const QVector<quint32> essenceObjs = wav.objectsWithProperty(essenceProp);
	QCOMPARE(essenceObjs.size(), 1);
	QVERIFY(wav.bytes(essenceObjs[0], essenceProp).isEmpty());
	QVERIFY(wav.bytes(essenceObjs[0], essenceProp, 16 * 1024 * 1024).size() > 8 * 1024 * 1024); // only when asked
	// value() is a view over the load-mode buffer; after open() there is none.
	QVERIFY(wav.value(essenceObjs[0], essenceProp).isEmpty());

	// Metadata does come through, by seek+read: three MOBJ objects with names.
	BentoFile again;
	QVERIFY(again.open(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav")));
	const int nameProp = again.propertyId("OMFI:CPNT:Name");
	const int mobIdProp = again.propertyId("OMFI:MOBJ:MobID");
	int named = 0;
	for (quint32 o : again.objectsWithProperty(mobIdProp))
	{
		QCOMPARE(again.objectClass(o), QByteArray("MOBJ"));
		if (!BentoFile::string(again.bytes(o, nameProp)).isEmpty())
			++named;
	}
	QVERIFY(named > 0);
	QVERIFY2(again.bytesRead() < kTailBudget, qPrintable(QString::number(again.bytesRead())));

	// The head object's mob index names the file mob the v2 PMR stores, whose
	// core bytes are the UID's [4:12]; the index's trailing word is junk.
	const int srcProp = again.propertyId("OMFI:SourceMobs");
	const QVector<quint32> heads = again.objectsWithProperty(srcProp);
	QCOMPARE(heads.size(), 1);
	QCOMPARE(again.objectClass(heads[0]), QByteArray("HEAD"));
	const QByteArray srcMobs = again.bytes(heads[0], srcProp);
	const QVector<BentoFile::MobIndexEntry> idx = BentoFile::mobIndex(srcMobs);
	QCOMPARE(idx.size(), 2);
	QCOMPARE(idx[0].uid.size(), 12);
	QCOMPARE(idx[0].uid.mid(4).toHex(), QByteArray("7429976a70397047"));
	QCOMPARE(again.objectClass(idx[0].object), QByteArray("MOBJ"));
	QCOMPARE(again.bytes(idx[0].object, mobIdProp), idx[0].uid);
	QVERIFY(BentoFile::handles(srcMobs).isEmpty());

	// A small .omf: the same budget, and the picture is only read when asked.
	BentoFile omf;
	QVERIFY2(omf.open(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), &why),
			 qPrintable(why));
	QVERIFY2(omf.bytesRead() < kTailBudget, qPrintable(QString::number(omf.bytesRead())));
	const int imageProp = omf.propertyId("OMFI:IDAT:ImageData");
	const QVector<quint32> images = omf.objectsWithProperty(imageProp);
	QCOMPARE(images.size(), 1);
	QVERIFY(omf.bytes(images[0], imageProp, 1024).isEmpty());
	QCOMPARE(omf.bytes(images[0], imageProp).size(), 8876);
	const QVector<BentoFile::MobIndexEntry> comp =
		BentoFile::mobIndex(omf.bytes(omf.objectsWithProperty(omf.propertyId("OMFI:CompositionMobs"))[0],
									  omf.propertyId("OMFI:CompositionMobs")));
	QCOMPARE(comp.size(), 1);
	QCOMPARE(comp[0].object, 68006u);
	QCOMPARE(omf.objectClass(comp[0].object), QByteArray("MOBJ"));
}

void TestBentoFile::omf_open_fails_cleanly_on_non_bento_input()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString why;

	// A plain RIFF stub: the label read happens (24 bytes), then nothing.
	QByteArray riff("RIFF\0\0\0\0WAVEfmt ", 16);
	riff.resize(1024);
	BentoFile b;
	QVERIFY(!b.open(writeTemp(tmp, "stub.wav", riff), &why));
	QVERIFY2(why.contains(QStringLiteral("label")), qPrintable(why));
	QCOMPARE(b.bytesRead(), qint64(24));
	QCOMPARE(b.entryCount(), 0);
	QVERIFY(b.bytes(1, 0).isEmpty());

	// Too small to hold a label: refused before any read.
	QVERIFY(!b.open(writeTemp(tmp, "tiny", QByteArray(10, 'x')), &why));
	QVERIFY2(why.contains(QStringLiteral("too small")), qPrintable(why));
	QCOMPARE(b.bytesRead(), qint64(0));

	// Missing file.
	QVERIFY(!b.open(tmp.filePath(QStringLiteral("absent.omf")), &why));
	QVERIFY2(why.contains(QStringLiteral("cannot open")), qPrintable(why));

	// TOC arithmetic that does not close, through the file path.
	BentoBuilder w;
	w.setString(w.addObject("MOBJ"), "OMFI:CPNT:Name", "x");
	QVERIFY(!b.open(writeTemp(tmp, "gap.bento", w.build(QByteArray(3, '\0'))), &why));
	QVERIFY2(why.contains(QStringLiteral("arithmetic")), qPrintable(why));
	QCOMPARE(b.bytesRead(), qint64(24));

	// A good builder file opens; a reuse after failure is clean.
	QVERIFY2(b.open(writeTemp(tmp, "good.bento", w.build()), &why), qPrintable(why));
	QCOMPARE(b.entryCount(), 4); // ObjID, the name, and two dictionary entries
	QCOMPARE(BentoFile::string(b.bytes(68000, b.propertyId("OMFI:CPNT:Name"))), QStringLiteral("x"));
}

void TestBentoFile::omf_mob_index()
{
	const QByteArray coreA = QByteArray::fromHex("129450353c599b28");
	const QByteArray coreB = QByteArray::fromHex("1294503545599b28");
	QByteArray index = BentoBuilder::le32(2).left(2);
	index += mobIndexRow(coreA, 68011, 0);
	index += mobIndexRow(coreB, 68020, 0x0012ee84); // the junk word Avid leaves behind

	BentoBuilder w;
	const quint32 head = w.addObject("HEAD");
	w.set(head, "OMFI:SourceMobs", index);
	BentoFile b;
	QVERIFY(b.load(w.build()));
	const QByteArray v = b.bytes(head, b.propertyId("OMFI:SourceMobs"));
	QCOMPARE(v, index);

	const QVector<BentoFile::MobIndexEntry> idx = BentoFile::mobIndex(v);
	QCOMPARE(idx.size(), 2);
	QCOMPARE(idx[0].uid, BentoBuilder::le32(0x2a) + coreA);
	QCOMPARE(idx[0].object, 68011u);
	QCOMPARE(idx[1].uid, BentoBuilder::le32(0x2a) + coreB);
	QCOMPARE(idx[1].object, 68020u);
	// handles() sees 8-byte pairs whose second word is never zero here.
	QVERIFY(BentoFile::handles(v).isEmpty());

	// Malformed shapes read as nothing: short, over-declared, zero object.
	QVERIFY(BentoFile::mobIndex(QByteArrayView("\x01")).isEmpty());
	const QByteArray overDeclared = BentoBuilder::le32(3).left(2) + index.mid(2);
	QVERIFY(BentoFile::mobIndex(overDeclared).isEmpty());
	QByteArray zero = BentoBuilder::le32(1).left(2) + mobIndexRow(coreA, 0, 0);
	QVERIFY(BentoFile::mobIndex(zero).isEmpty());
	QVERIFY(BentoFile::mobIndex(QByteArrayView()).isEmpty());
}

QTEST_APPLESS_MAIN(TestBentoFile)
#include "tst_bentofile.moc"
