// BentoFile: the container primitive under msmMMOB.mdb. Label + TOC
// arithmetic, immediate vs offset values, first-in-file-wins on duplicate
// entries, the file's own property-name dictionary, and the typed readers —
// against builder-made containers and the three real MDB fixtures.

#include "bentofile.h"
#include "testbento.h"

#include <QByteArray>
#include <QFile>
#include <QTest>

namespace
{
	QByteArray readFile(const QString &path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}
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
	};
	const Fx fixtures[] = {
		{FIXTURES_DIR "/msmMMOB.mdb", 1432, 700},
		{FIXTURES_DIR "/msmMMOB_macroman.mdb", -1, 700},
		{FIXTURES_DIR "/corpus_headers/msmMMOB_round3.mdb", 221406, 700},
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
		// Every MOBJ object's class reads as MOBJ, and its MobID starts 06 0a 2b 34.
		for (quint32 o : mobs)
		{
			QCOMPARE(b.objectClass(o), QByteArray("MOBJ"));
			QVERIFY(b.value(o, mobIdProp).toByteArray().startsWith(QByteArray::fromHex("060a2b34")));
		}
	}
	// The round-3 database: 1,392 MOBJ objects carry a MobID (1,380 distinct).
	BentoFile r3;
	QVERIFY(r3.load(readFile(QStringLiteral(FIXTURES_DIR "/corpus_headers/msmMMOB_round3.mdb"))));
	QCOMPARE(r3.objectsWithProperty(r3.propertyId("OMFI:MOBJ:MobID")).size(), 1392);
}

QTEST_APPLESS_MAIN(TestBentoFile)
#include "tst_bentofile.moc"
