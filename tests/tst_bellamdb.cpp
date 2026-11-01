// Authored typed Bento controls exercise Bella's contract directly. Genuine
// database checks below use pinned identities and recorded field expectations;
// no OldScanner reader or projection supplies their expected values.

#include "bella/mdbparser.h"
#include "testmediaenginebento.h"

#include <QFile>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>

namespace
{
	using TestMediaEngineBento::number;
	using TestMediaEngineBento::TypedBento;
	QByteArray uid(quint32 serial, bool big = false)
	{
		return number<quint32>(42, big) + number(serial, big) + number<quint32>(900, big);
	}

	QByteArray expectedUid(quint32 serial)
	{
		return QByteArray::fromHex("060a2b340101010101010f0013000000") + number(serial) +
			   number<quint32>(900) + QByteArray::fromHex("060e2b347f7f2a80");
	}

	QByteArray bytes(const Bella::MobId &id)
	{
		return QByteArray(id.bytes().data(), id.bytes().size());
	}

	void object(TypedBento &writer, quint32 handle, const QByteArray &cls)
	{
		writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", cls, true);
	}

	void replace(TypedBento &writer, quint32 owner, const QByteArray &name,
				 const QByteArray &value, const QByteArray &type = {})
	{
		const quint32 property = writer.properties.value(name);
		for (auto &entry : writer.entries)
			if (entry.object == owner && entry.property == property)
			{
				entry.bytes = value;
				if (!type.isEmpty())
					entry.type = writer.type(type);
				entry.immediate = value.size() <= 4;
				return;
			}
		qFatal("Authored MDB fixture has no property to replace");
	}

	void members(TypedBento &writer, const QVector<quint32> &handles)
	{
		QByteArray encoded = number(quint16(handles.size()), writer.metadataBig);
		for (const auto handle : handles)
			encoded += writer.reference(handle, 1);
		replace(writer, 1, "OMFI:ObjectSpine", encoded);
	}

	void rate(TypedBento &writer, quint32 owner, const QByteArray &property,
			  qint32 numerator, qint32 denominator, const QByteArray &type = "omfi:ExactEditRate")
	{
		writer.add(owner, property, type,
				   number(numerator, writer.metadataBig) + number(denominator, writer.metadataBig));
	}

	void master(TypedBento &writer, quint32 handle, quint32 track, quint32 clip,
				quint32 identity, quint32 target, const QByteArray &name = "Master")
	{
		object(writer, handle, "MOBJ");
		writer.add(handle, "OMFI:MOBJ:MobID", "omfi:UID", uid(identity, writer.metadataBig));
		writer.add(handle, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", number<quint32>(7, writer.metadataBig), true);
		writer.add(handle, "OMFI:CPNT:Name", "omfi:String", name + '\0');
		writer.referenceArray(handle, "OMFI:TRKG:Tracks", {track}, 1);
		object(writer, track, "TRAK");
		writer.add(track, "OMFI:TRAK:LabelNumber", "omfi:Short", number<qint16>(1, writer.metadataBig), true);
		writer.add(track, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(clip, 1));
		object(writer, clip, "SCLP");
		writer.add(clip, "OMFI:SCLP:SourceID", "omfi:UID", uid(target, writer.metadataBig));
	}

	TypedBento video(bool big = false)
	{
		TypedBento writer;
		writer.metadataBig = big;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", {101, 301}, 1);
		object(writer, 101, "MOBJ");
		writer.add(101, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<quint16>(0, big), true);
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11, big));
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		rate(writer, 101, "OMFI:CPNT:EditRate", 25, 1);
		object(writer, 201, "CDCI");
		rate(writer, 201, "OMFI:MDFL:SampleRate", 25, 1);
		writer.add(201, "OMFI:MDFL:Length", "omfi:Long", number<qint32>(250, big), true);
		writer.add(201, "OMFI:DIDD:StoredWidth", "omfi:Long", number<qint32>(1920, big), true);
		writer.add(201, "OMFI:DIDD:StoredHeight", "omfi:Long", number<qint32>(540, big), true);
		writer.add(201, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", number<qint16>(1, big), true);
		writer.add(201, "OMFI:CDCI:ComponentWidth", "omfi:Long", number<qint32>(10, big), true);
		writer.add(201, "OMFI:CDCI:HorizontalSubsampling", "omfi:Ulong", number<quint32>(2, big), true);
		writer.add(201, "OMFI:CDCI:VerticalSubsampling", "omfi:Ulong", number<quint32>(1, big), true);
		master(writer, 301, 401, 501, 22, 11);
		return writer;
	}

	TypedBento audio(const QByteArray &descriptor, bool big = false)
	{
		auto writer = video(big);
		replace(writer, 201, "OMFI:ObjID", descriptor);
		replace(writer, 201, "OMFI:MDFL:SampleRate", number<qint32>(48000, big) + number<qint32>(1, big));
		replace(writer, 201, "OMFI:MDFL:Length", number<qint32>(96000, big));
		return writer;
	}

	struct Attribute
	{
		QByteArray name;
		QByteArray text;
		quint16 kind = 2;
		quint32 target = 0;
		qint32 integer = 0;
	};

	void attributes(TypedBento &writer, quint32 owner, quint32 firstHandle,
					const QVector<Attribute> &values)
	{
		object(writer, firstHandle, "ATTR");
		if (owner)
			writer.add(owner, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(firstHandle, 1));
		QVector<quint32> entries;
		for (qsizetype index = 0; index < values.size(); ++index)
		{
			const quint32 handle = firstHandle + 1 + quint32(index);
			const auto &value = values[index];
			entries.append(handle);
			object(writer, handle, "ATTB");
			writer.add(handle, "OMFI:ATTB:Name", "omfi:String", value.name + '\0');
			writer.add(handle, "OMFI:ATTB:Kind", "omfi:AttrKind", number(value.kind, writer.metadataBig), true);
			if (value.kind == 1)
				writer.add(handle, "OMFI:ATTB:IntAttribute", "omfi:Long", number(value.integer, writer.metadataBig), true);
			else if (value.kind == 3)
				writer.add(handle, "OMFI:ATTB:ObjAttribute", "omfi:ObjRef", writer.reference(value.target, 1));
			else if (value.kind == 4)
			{
				const auto encoded = value.text + '\0';
				writer.add(handle, "OMFI:ATTB:BobSize", "omfi:Long", number<qint32>(qint32(encoded.size()), writer.metadataBig), true);
				writer.add(handle, "OMFI:ATTB:BobData", "omfi:VarLenBytes", encoded);
			}
			else
				writer.add(handle, "OMFI:ATTB:StringAttribute", "omfi:String", value.text + '\0');
		}
		writer.referenceArray(firstHandle, "OMFI:ATTR:AttrRefs", entries, 1);
	}

	void source(TypedBento &writer, quint32 handle, quint32 serial, const QVector<Attribute> &projects)
	{
		object(writer, handle, "MOBJ");
		writer.add(handle, "OMFI:MOBJ:MobID", "omfi:UID", uid(serial, writer.metadataBig));
		// Reserve disjoint authored attribute handles for neighbouring source mobs.
		attributes(writer, handle, 20000 + handle * 16, projects);
	}

	void immediateSources(TypedBento &writer, const QVector<quint32> &serials)
	{
		QVector<quint32> tracks;
		for (qsizetype index = 0; index < serials.size(); ++index)
		{
			const quint32 track = 1101 + quint32(index * 2);
			const quint32 clip = track + 1;
			tracks.append(track);
			object(writer, track, "TRAK");
			writer.add(track, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(clip, 1));
			object(writer, clip, "SCLP");
			writer.add(clip, "OMFI:SCLP:SourceID", "omfi:UID", uid(serials[index], writer.metadataBig));
		}
		writer.referenceArray(101, "OMFI:TRKG:Tracks", tracks, 1);
	}

	QByteArray codingWire(const QByteArray &label, bool big)
	{
		Q_ASSERT(label.size() == 16);
		if (big)
			return label.mid(8) + label.first(8);
		const auto material = label.mid(8);
		QByteArray encoded;
		for (int index : {3, 2, 1, 0, 5, 4, 7, 6})
			encoded += material[index];
		return encoded + label.first(8);
	}

	QByteArray chunk(const QByteArray &id, const QByteArray &payload, bool big)
	{
		return id + number(quint32(payload.size()), big) + payload + QByteArray(payload.size() & 1, '\0');
	}

	QByteArray waveSummary(quint32 frequency = 48000, quint16 bits = 24)
	{
		const quint16 alignment = quint16(bits / 8);
		const auto format = number<quint16>(1) + number<quint16>(1) + number(frequency) +
							number<quint32>(frequency * alignment) + number(alignment) + number(bits);
		const auto fields = chunk("fmt ", format, false) + "data" + number<quint32>(6000000);
		// An MDB Summary omits the samples; its copied RIFF/data extents may exceed it.
		return QByteArray("RIFF") + number<quint32>(6000000 + quint32(fields.size()) + 4) + "WAVE" + fields;
	}

	QByteArray aiffSummary(const QByteArray &frequency, bool compressed)
	{
		QByteArray common = number<qint16>(1, true) + number<quint32>(2000000, true) +
							number<qint16>(24, true) + frequency;
		if (compressed)
			common += QByteArray("in24\0\0", 6);
		const auto fields = (compressed ? chunk("FVER", number<quint32>(0xa2805140, true), true) : QByteArray{}) +
							chunk("COMM", common, true) + "SSND" + number<quint32>(6000008, true) + QByteArray(8, '\0');
		return QByteArray("FORM") + number<quint32>(6000000 + quint32(fields.size()) + 4, true) +
			   (compressed ? "AIFC" : "AIFF") + fields;
	}

	Bella::MdbReport parse(const QByteArray &capture, bool details = true)
	{
		QTemporaryFile source;
		if (!source.open() || source.write(capture) != capture.size() || !source.flush())
			qFatal("Cannot write authored MDB fixture");
		source.close();
		const std::atomic_bool stop{false};
		return Bella::parseMdb(source.fileName(), stop, details);
	}

	Bella::MdbReport parse(const TypedBento &writer, bool details = true)
	{
		return parse(writer.build(), details);
	}

	const Bella::MdbFileRecord *file(const Bella::MdbReport &report, const QByteArray &identity)
	{
		for (const auto &candidate : report.catalogue.files)
			if (bytes(candidate.fileMobId) == identity)
				return &candidate;
		return nullptr;
	}

	TypedBento lengthFixture(bool big)
	{
		auto writer = video(big);
		attributes(writer, 101, 17000, {{"_PJ", "Independent Project"}});
		return writer;
	}

	QString independentVideoFactsError(const Bella::MdbReport &report, Bella::MdbType expectedType = Bella::MdbType::Media,
									   bool expectedMasterLinksComplete = true, const QString &expectedMasterName = QStringLiteral("Master"))
	{
		if (report.reading != Bella::MdbReadResult::Completed || report.sizeDateCheck != Bella::MdbCheckResult::Verified ||
			report.catalogue.files.size() != 1 || report.catalogue.masters.size() != 1)
			return QStringLiteral("Source/check/catalogue result changed");
		const auto &facts = report.catalogue.files.first();
		if (bytes(facts.fileMobId) != expectedUid(11) || !facts.ownDescriptorQualified || facts.kind != Bella::MdbKind::Video ||
			facts.project != QStringLiteral("Independent Project") || facts.projectSelection != Bella::MdbProjectSelection::OwnFile ||
			facts.projectKey != Bella::MdbProjectKey::Pj)
			return QStringLiteral("Independent file identity, owner or Project changed");
		if (!facts.sampleRate || facts.sampleRate->words.numerator != 25 || facts.sampleRate->words.denominator != 1 ||
			facts.sampleRate->encoding != Bella::MdbRateEncoding::ExactEditRate || !facts.resolution ||
			facts.resolution->dimensions.width != 1920 || facts.resolution->dimensions.height != 1080 ||
			facts.bitDepth.bits != std::optional<quint32>(10) || facts.numbering != Bella::MdbNumbering::NotRequired)
			return QStringLiteral("Independent rate, raster, depth or numbering changed");
		const auto &master = report.catalogue.masters.first();
		if (bytes(master.masterMobId) != expectedUid(22) || master.clipName != expectedMasterName || master.type != expectedType)
			return QStringLiteral("Independent master facts changed");
		const QVector<qsizetype> expectedLinks = expectedMasterLinksComplete ? QVector<qsizetype>{0} : QVector<qsizetype>{};
		if (!report.catalogue.fileReferencesComplete || facts.masterLinksComplete != expectedMasterLinksComplete || facts.directMasterRecords != expectedLinks)
			return QStringLiteral("Independent reference coverage or master links changed");
		return {};
	}

	TypedBento codingLookupFixture(bool big, int variation)
	{
		auto writer = lengthFixture(big);
		const auto label = variation == 10 ? QByteArray(16, '\0') : QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10");
		if (variation != 1)
			writer.add(201, "OMFI:DIDD:EssenceCompression", variation == 4 ? "omfi:String" : "omfi:GUID",
					   variation == 2 ? codingWire(label, big).first(15) : codingWire(label, big));
		if (variation == 3 || variation == 9)
			writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:GUID", codingWire(variation == 9 ? label : QByteArray(16, char(0x55)), big));
		if (variation == 5 || variation == 6 || variation == 7 || variation == 8 || variation == 11)
			writer.add(201, "OMFI:ByteOrder", variation == 11 ? "omfi:String" : "omfi:Short",
					   variation == 7 ? QByteArray("III") : QByteArray((variation == 6 ? !big : big) ? "MM" : "II"), true);
		if (variation == 8)
			writer.add(201, "OMFI:ByteOrder", "omfi:Short", big ? "II" : "MM", true);
		return writer;
	}

	TypedBento sharedMasterAttributesFixture(bool big, int variation)
	{
		auto writer = lengthFixture(big);
		replace(writer, 301, "OMFI:MOBJ:UsageCode", number<quint32>(1, big));
		if (variation == 1)
			attributes(writer, 301, 18000, {});
		else if (variation == 2)
		{
			object(writer, 18000, "ATTR");
			writer.add(301, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(18000, 1));
		}
		else if (variation != 0)
		{
			attributes(writer, 301, 18000, {{"_ORG_BIN", {}, 3, 18100}, {"_IMPORTSETTING", {}, 3, 0}});
			object(writer, 18100, "MCBR");
			writer.add(18100, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", variation == 10 ? QByteArray::fromHex("c32800") : QByteArray("Bin\0", 4));
			writer.add(18100, "OMFI:MCBR:MC:binName", "omfi:String", QByteArray("Legacy\0", 7));
			writer.add(501, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<qint16>(1, big), true);
			replace(writer, 301, "OMFI:TRKG:Tracks", number<quint16>(2, big) + writer.reference(401, 1) + writer.reference(402, 1));
			object(writer, 402, "TRAK");
			writer.add(402, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(502, 1));
			object(writer, 502, "SCLP");
			writer.add(502, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<qint16>(1, big), true);
			writer.add(502, "OMFI:SCLP:SourceID", "omfi:UID", uid(11, big));
		}
		if (variation == 3 || variation == 6)
			replace(writer, 18000, "OMFI:ObjID", QByteArray("MOBJ"));
		else if (variation == 4)
			replace(writer, 18002, "OMFI:ObjID", QByteArray("MOBJ"));
		else if (variation == 5)
		{
			attributes(writer, 0, 18400, {});
			writer.add(301, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(18400, 1));
		}
		else if (variation == 8)
			replace(writer, 18002, "OMFI:ATTB:ObjAttribute", QByteArray(3, '\0'));
		else if (variation == 9)
			replace(writer, 501, "OMFI:CPNT:TrackKind", QByteArray(1, char(1)));
		if (variation == 6 || variation == 10)
			writer.add(301, "OMFI:CPNT:Name", "omfi:String", QByteArray("Different\0", 10));
		return writer;
	}

	void timecode(TypedBento &writer, quint32 flags, bool indirect)
	{
		const auto clock = number<qint32>(2997, writer.metadataBig) + number<qint32>(100, writer.metadataBig);
		replace(writer, 201, "OMFI:MDFL:SampleRate", clock);
		replace(writer, 101, "OMFI:CPNT:EditRate", clock);
		const quint32 owner = indirect ? 601 : 101;
		if (indirect)
		{
			members(writer, {101, 301, 601});
			object(writer, 601, "MOBJ");
			writer.add(601, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<quint16>(0, writer.metadataBig), true);
			rate(writer, 601, "OMFI:CPNT:EditRate", 2997, 100);
			writer.add(601, "OMFI:MOBJ:MobID", "omfi:UID", uid(33, writer.metadataBig));
			writer.referenceArray(101, "OMFI:TRKG:Tracks", {611}, 1);
			object(writer, 611, "TRAK");
			writer.add(611, "OMFI:TRAK:LabelNumber", "omfi:Short", number<qint16>(1, writer.metadataBig), true);
			writer.add(611, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(612, 1));
			object(writer, 612, "SCLP");
			writer.add(612, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<quint16>(1, writer.metadataBig), true);
			rate(writer, 612, "OMFI:CPNT:EditRate", 2997, 100);
			writer.add(612, "OMFI:CLIP:Length", "omfi:Long", number<qint32>(250, writer.metadataBig), true);
			writer.add(612, "OMFI:SCLP:SourceID", "omfi:UID", uid(33, writer.metadataBig));
			writer.add(612, "OMFI:SCLP:SourceTrack", "omfi:Short", number<qint16>(1, writer.metadataBig), true);
			writer.add(612, "OMFI:SCLP:SourcePosition", "omfi:Long", number<qint32>(0, writer.metadataBig), true);
		}
		writer.referenceArray(owner, "OMFI:TRKG:Tracks", {621, 623}, 1);
		for (const quint32 track : {621u, 623u})
		{
			object(writer, track, "TRAK");
			writer.add(track, "OMFI:TRAK:LabelNumber", "omfi:Short", number<qint16>(1, writer.metadataBig), true);
			writer.add(track, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(track + 1, 1));
			object(writer, track + 1, track == 621 ? "FILL" : "TCCP");
			writer.add(track + 1, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<quint16>(track == 621 ? 1 : 3, writer.metadataBig), true);
			rate(writer, track + 1, "OMFI:CPNT:EditRate", 2997, 100);
			writer.add(track + 1, "OMFI:CLIP:Length", "omfi:Long", number<qint32>(250, writer.metadataBig), true);
		}
		writer.add(624, "OMFI:TCCP:Flags", "omfi:Long", number(flags, writer.metadataBig), true);
		writer.add(624, "OMFI:TCCP:FPS", "omfi:Short", number<qint16>(30, writer.metadataBig), true);
		writer.add(624, "OMFI:TCCP:StartTC", "omfi:Long", number<qint32>(0, writer.metadataBig), true);
	}
}

class TestBellaMdb final : public QObject
{
	Q_OBJECT
private slots:
	void reportDefaults();
	void preCancelledDoesNotOpen();
	void missingSource();
	void emptyCatalogue();
	void referenceArrayExtentAcceptsCountMarker();
	void unsupportedLayouts_data();
	void unsupportedLayouts();
	void damagedSharedFoundation_data();
	void damagedSharedFoundation();
	void ownedVideoFacts_data();
	void ownedVideoFacts();
	void descriptorKindKeepsQualifiedTiming_data();
	void descriptorKindKeepsQualifiedTiming();
	void originalRateEncoding_data();
	void originalRateEncoding();
	void zeroLengthIsKnown();
	void legacyLengthValues_data();
	void legacyLengthValues();
	void legacyLengthRejectsOtherForms_data();
	void legacyLengthRejectsOtherForms();
	void legacyLengthMixedDuplicates_data();
	void legacyLengthMixedDuplicates();
	void localDamageKeepsIndependentFacts_data();
	void localDamageKeepsIndependentFacts();
	void pcmaWrongNumericTypeDoesNotInventDepth();
	void duplicateOwnersRemainSeparate();
	void masterOwnerLinksRemainSeparate();
	void incompleteMasterGraphDoesNotProveUnique();
	void missingTrackComponentDoesNotProveUnique();
	void unknownDictionaryCannotProveCodingAbsence();
	void codingByteOrderLookup_data();
	void codingByteOrderLookup();
	void optionalIndexIdentity_data();
	void optionalIndexIdentity();
	void projectSelection_data();
	void projectSelection();
	void unreadableProjectListKeepsProblems_data();
	void unreadableProjectListKeepsProblems();
	void originalBinUtf8Fallback_data();
	void originalBinUtf8Fallback();
	void originalBinConflictBoundary_data();
	void originalBinConflictBoundary();
	void audioSourceRepresentations_data();
	void audioSourceRepresentations();
	void unsupportedWaveStorage_data();
	void unsupportedWaveStorage();
	void zeroWaveWidthProblem_data();
	void zeroWaveWidthProblem();
	void aiffInvalidFrequencyIsLocal_data();
	void aiffInvalidFrequencyIsLocal();
	void aiffBoundedFractionEdges_data();
	void aiffBoundedFractionEdges();
	void audioSummaryDamageBoundaries_data();
	void audioSummaryDamageBoundaries();
	void audioSummaryFramingProblems_data();
	void audioSummaryFramingProblems();
	void specialDepthCodes_data();
	void specialDepthCodes();
	void unrelatedCodingDoesNotValidateDepthSentinel();
	void rgbaKeepsSeparateArrays();
	void rgbaMismatchedArraysDoNotGuessDepth();
	void numbering_data();
	void numbering();
	void numberingDamageKeepsCountAndClock();
	void knownNdfClockDoesNotNeedDamagedFlag();
	void optionalDetailsDoNotRemoveType();
	void precomputeImportKindBoundary_data();
	void precomputeImportKindBoundary();
	void sharedMasterAttributes_data();
	void sharedMasterAttributes();
	void genuineDatabases_data();
	void genuineDatabases();
	void genuineAudioFieldsAndIdentities();
};

void TestBellaMdb::reportDefaults()
{
	const Bella::MdbReport report;
	QCOMPARE(report.reading, Bella::MdbReadResult::NotRead);
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::NotChecked);
	QVERIFY(report.path.isEmpty());
	QVERIFY(report.catalogue.files.isEmpty());
	QVERIFY(report.catalogue.masters.isEmpty());
	QVERIFY(!report.catalogue.fileReferencesComplete);
	QVERIFY(report.problemNotes.isEmpty());
}

void TestBellaMdb::preCancelledDoesNotOpen()
{
	const std::atomic_bool stop{true};
	const auto report = Bella::parseMdb(QStringLiteral("/no-such-bella-mdb/source.mdb"), stop, false);
	QCOMPARE(report.reading, Bella::MdbReadResult::Cancelled);
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::NotChecked);
	QCOMPARE(report.path, QStringLiteral("/no-such-bella-mdb/source.mdb"));
	QVERIFY(report.catalogue.files.isEmpty());
	QVERIFY(report.catalogue.masters.isEmpty());
}

void TestBellaMdb::missingSource()
{
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	const std::atomic_bool stop{false};
	const auto report = Bella::parseMdb(folder.filePath(QStringLiteral("missing.mdb")), stop, false);
	QCOMPARE(report.reading, Bella::MdbReadResult::IoError);
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::NotChecked);
	QVERIFY(!report.problemNotes.isEmpty());
	QVERIFY(!report.catalogue.fileReferencesComplete);
}

void TestBellaMdb::emptyCatalogue()
{
	TypedBento writer;
	writer.head(1);
	writer.referenceArray(1, "OMFI:ObjectSpine", {}, 1);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::Verified);
	QVERIFY(report.catalogue.files.isEmpty());
	QVERIFY(report.catalogue.masters.isEmpty());
	QVERIFY(report.catalogue.fileReferencesComplete);
}

void TestBellaMdb::referenceArrayExtentAcceptsCountMarker()
{
	auto writer = video();
	replace(writer, 1, "OMFI:ObjectSpine", number<quint16>(0xffff) + writer.reference(101, 1) + writer.reference(301, 1));
	replace(writer, 301, "OMFI:TRKG:Tracks", number<quint16>(0xffff) + writer.reference(401, 1));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.files.size(), 1);
	QCOMPARE(report.catalogue.masters.size(), 1);
	QVERIFY(report.catalogue.fileReferencesComplete);
	QVERIFY(report.catalogue.files.first().masterLinksComplete);
	QCOMPARE(report.catalogue.files.first().directMasterRecords.size(), 1);
}

void TestBellaMdb::unsupportedLayouts_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("Bento2") << 0;
	QTest::newRow("OMF2") << 1;
}

void TestBellaMdb::unsupportedLayouts()
{
	QFETCH(int, variation);
	TypedBento writer;
	writer.major = variation == 0 ? 2 : 1;
	writer.head(variation == 0 ? 1 : 2);
	writer.referenceArray(1, variation == 0 ? "OMFI:ObjectSpine" : "OMFI:HEAD:Mobs", {}, variation == 0 ? 1 : 2);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Unsupported);
	QVERIFY(report.catalogue.files.isEmpty());
	QVERIFY(report.catalogue.masters.isEmpty());
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::damagedSharedFoundation_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("empty capture") << 0;
	QTest::newRow("bad label magic") << 1;
	QTest::newRow("TOC extent outside image") << 2;
	QTest::newRow("incomplete TOC entry") << 3;
	QTest::newRow("unresolved shared catalogue") << 4;
}

void TestBellaMdb::damagedSharedFoundation()
{
	QFETCH(int, variation);
	auto writer = video();
	if (variation == 4)
		members(writer, {101, 301, 999});
	auto capture = writer.build();
	if (variation == 0)
		capture.clear();
	else if (variation == 1)
		capture[capture.size() - 24] = '\0';
	else if (variation == 2)
		qToLittleEndian<quint32>(std::numeric_limits<quint32>::max(), capture.data() + capture.size() - 8);
	else if (variation == 3)
	{
		const auto tocSize = qFromLittleEndian<quint32>(capture.constData() + capture.size() - 4);
		qToLittleEndian<quint32>(tocSize - 1, capture.data() + capture.size() - 4);
	}
	const auto report = parse(capture);
	QCOMPARE(report.reading, Bella::MdbReadResult::Malformed);
	QVERIFY(report.catalogue.files.isEmpty());
	QVERIFY(report.catalogue.masters.isEmpty());
	QVERIFY(!report.catalogue.fileReferencesComplete);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::ownedVideoFacts_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::ownedVideoFacts()
{
	QFETCH(bool, big);
	const auto report = parse(video(big));
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::Verified);
	QCOMPARE(report.catalogue.files.size(), 1);
	QCOMPARE(report.catalogue.masters.size(), 1);
	QVERIFY(report.catalogue.fileReferencesComplete);
	// The temporary source and authored writer are already gone.
	QVERIFY(!QFile::exists(report.path));
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(bytes(facts.fileMobId), expectedUid(11));
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.kind, Bella::MdbKind::Video);
	QVERIFY(facts.descriptorClass);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(facts.descriptorClass->data()), 4), QByteArray("CDCI"));
	QVERIFY(facts.length);
	QCOMPARE(*facts.length, 250);
	QVERIFY(facts.sampleRate);
	QCOMPARE(facts.sampleRate->words.numerator, 25);
	QCOMPARE(facts.sampleRate->words.denominator, 1);
	QCOMPARE(facts.sampleRate->encoding, Bella::MdbRateEncoding::ExactEditRate);
	QCOMPARE(facts.numbering, Bella::MdbNumbering::NotRequired);
	QVERIFY(facts.resolution);
	QCOMPARE(facts.resolution->dimensions.width, 1920);
	QCOMPARE(facts.resolution->dimensions.height, 1080);
	QVERIFY(facts.compression.namingGeometry);
	QCOMPARE(facts.compression.namingGeometry->height, 1080);
	QCOMPARE(facts.compression.codingState, Bella::MdbCodingState::Absent);
	QCOMPARE(facts.compression.horizontalSubsampling, std::optional<qint32>(2));
	QCOMPARE(facts.compression.verticalSubsampling, std::optional<qint32>(1));
	QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(10));
	QVERIFY(facts.masterLinksComplete);
	QCOMPARE(facts.directMasterRecords.size(), 1);
	const auto index = facts.directMasterRecords.first();
	QVERIFY(index >= 0 && index < report.catalogue.masters.size());
	const auto &clip = report.catalogue.masters[index];
	QCOMPARE(bytes(clip.masterMobId), expectedUid(22));
	QCOMPARE(clip.clipName, QStringLiteral("Master"));
	QCOMPARE(clip.type, Bella::MdbType::Media);
}

void TestBellaMdb::descriptorKindKeepsQualifiedTiming_data()
{
	QTest::addColumn<bool>("audioFormat");
	QTest::newRow("JPED keeps picture timing with Video Kind") << false;
	QTest::newRow("MPGA keeps audio editing clock with Unknown Kind") << true;
}

void TestBellaMdb::descriptorKindKeepsQualifiedTiming()
{
	QFETCH(bool, audioFormat);
	auto writer = audioFormat ? audio("MPGA") : video();
	if (!audioFormat)
		replace(writer, 201, "OMFI:ObjID", "JPED");
	const auto checked = parse(writer);
	QCOMPARE(checked.reading, Bella::MdbReadResult::Completed);
	const auto &facts = checked.catalogue.files.first();
	QCOMPARE(facts.kind, audioFormat ? Bella::MdbKind::Unknown : Bella::MdbKind::Video);
	QVERIFY(facts.ownDescriptorQualified);
	QVERIFY(facts.length);
	QVERIFY(facts.sampleRate);
	QCOMPARE(facts.numbering, Bella::MdbNumbering::NotRequired);
	if (audioFormat)
	{
		QVERIFY(facts.audioEditRate);
		QCOMPARE(facts.audioEditRate->words.numerator, 25);
		QVERIFY(!facts.audioFrequency);
	}
	else
		QVERIFY(facts.resolution);
}

void TestBellaMdb::originalRateEncoding_data()
{
	QTest::addColumn<bool>("exact");
	QTest::newRow("ExactEditRate") << true;
	QTest::newRow("Rational") << false;
}

void TestBellaMdb::originalRateEncoding()
{
	QFETCH(bool, exact);
	auto writer = video();
	replace(writer, 201, "OMFI:MDFL:SampleRate", number<qint32>(2997) + number<qint32>(100), exact ? "omfi:ExactEditRate" : "omfi:Rational");
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QVERIFY(report.catalogue.files.first().sampleRate);
	const auto &sourceRate = *report.catalogue.files.first().sampleRate;
	QCOMPARE(sourceRate.words.numerator, 2997);
	QCOMPARE(sourceRate.words.denominator, 100);
	QCOMPARE(sourceRate.encoding, exact ? Bella::MdbRateEncoding::ExactEditRate : Bella::MdbRateEncoding::Rational);
	if (exact)
		QCOMPARE(report.catalogue.files.first().numbering, Bella::MdbNumbering::MissingNdfFallback);
}

void TestBellaMdb::zeroLengthIsKnown()
{
	auto writer = video();
	replace(writer, 201, "OMFI:MDFL:Length", number<qint32>(0));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QVERIFY(report.catalogue.files.first().length);
	QCOMPARE(*report.catalogue.files.first().length, 0);
	QVERIFY(report.catalogue.files.first().sampleRate);
}

void TestBellaMdb::legacyLengthValues_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::legacyLengthValues()
{
	QFETCH(bool, big);
	const std::array<qint32, 5> values{{0, 250, std::numeric_limits<qint32>::max(), -1, std::numeric_limits<qint32>::min()}};
	for (qsizetype index = 0; index <= qsizetype(values.size()); ++index)
	{
		const bool present = index < qsizetype(values.size());
		const auto context = present ? QStringLiteral("Long4 count %1").arg(values[std::size_t(index)]) : QStringLiteral("Missing Length");
		auto writer = lengthFixture(big);
		if (present)
			replace(writer, 201, "OMFI:MDFL:Length", number(values[std::size_t(index)], big));
		else
		{
			const auto property = writer.properties.value("OMFI:MDFL:Length");
			writer.entries.erase(std::remove_if(writer.entries.begin(), writer.entries.end(), [property](const auto &entry)
												{ return entry.object == 201 && entry.property == property; }),
								 writer.entries.end());
		}
		const auto report = parse(writer);
		const auto independent = independentVideoFactsError(report);
		QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
		const auto expected = present && values[std::size_t(index)] >= 0 ? std::optional<qint64>(values[std::size_t(index)]) : std::nullopt;
		QVERIFY2(report.catalogue.files.first().length == expected, qPrintable(context));
		const QStringList notes = present && values[std::size_t(index)] < 0 ? QStringList({QStringLiteral("Object 201, OMFI:MDFL:Length: Negative length is unavailable.")}) : QStringList{};
		QVERIFY2(report.problemNotes == notes, qPrintable(context + ": " + report.problemNotes.join('\n')));
	}
}

void TestBellaMdb::legacyLengthRejectsOtherForms_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::legacyLengthRejectsOtherForms()
{
	QFETCH(bool, big);
	QVector<QPair<QByteArray, QByteArray>> forms;
	const auto addTypes = [&forms](std::initializer_list<const char *> types, const QByteArray &payload)
	{
		for (const auto *type : types)
			forms.append({QByteArray(type), payload});
	};
	// All 19 alternative type/width pairs accepted by the prior generic decoder.
	addTypes({"omfi:Short", "omfi:Int16", "omfi:TrackType", "omfi:UsageCodeType", "omfi:AttrKind", "omfi:LayoutType"}, number<qint16>(250, big));
	addTypes({"omfi:UInt16", "omfi:Ushort", "omfi:UShort"}, number<quint16>(250, big));
	addTypes({"omfi:Int32", "omfi:Length32", "omfi:Position32", "omfi:UsageCodeType"}, number<qint32>(250, big));
	addTypes({"omfi:UInt32", "omfi:Ulong", "omfi:ULong"}, number<quint32>(250, big));
	addTypes({"omfi:Int64", "omfi:Length64", "omfi:Position64"}, number<qint64>(250, big));
	QCOMPARE(forms.size(), 19);
	forms.append({"omfi:Rational", number<qint32>(250, big) + number<qint32>(1, big)});
	forms.append({"omfi:Long", QByteArray{}});
	forms.append({"omfi:Long", number<qint32>(250, big).first(3)});
	forms.append({"omfi:Long", number<qint32>(250, big) + QByteArray(1, '\0')});
	forms.append({"omfi:Long", number<qint64>(250, big)});
	for (const auto &form : forms)
	{
		const auto context = QStringLiteral("%1, %2 bytes, %3").arg(QString::fromLatin1(form.first)).arg(form.second.size()).arg(QString::fromLatin1(form.second.toHex()));
		auto writer = lengthFixture(big);
		replace(writer, 201, "OMFI:MDFL:Length", form.second, form.first);
		const auto report = parse(writer);
		const auto independent = independentVideoFactsError(report);
		QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
		QVERIFY2(!report.catalogue.files.first().length, qPrintable(context));
		QVERIFY2(report.problemNotes == QStringList({QStringLiteral("Object 201, OMFI:MDFL:Length: Numeric value has an unsupported type, incomplete width, byte order or conflicting values.")}),
				 qPrintable(context + ": " + report.problemNotes.join('\n')));
	}
}

void TestBellaMdb::legacyLengthMixedDuplicates_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::legacyLengthMixedDuplicates()
{
	QFETCH(bool, big);
	const auto valid = number<qint32>(250, big);
	const QVector<QPair<QByteArray, QByteArray>> invalid{{"omfi:Ushort", number<quint16>(250, big)},
														 {"omfi:Length32", valid},
														 {"omfi:Long", number<qint16>(250, big)}};
	for (const auto &form : invalid)
		for (const bool invalidFirst : {false, true})
		{
			const auto context = QStringLiteral("%1/%2 bytes %3 Long4; both encode 250")
									 .arg(QString::fromLatin1(form.first))
									 .arg(form.second.size())
									 .arg(invalidFirst ? QStringLiteral("before") : QStringLiteral("after"));
			auto writer = lengthFixture(big);
			if (invalidFirst)
			{
				replace(writer, 201, "OMFI:MDFL:Length", form.second, form.first);
				writer.add(201, "OMFI:MDFL:Length", "omfi:Long", valid, true);
			}
			else
				writer.add(201, "OMFI:MDFL:Length", form.first, form.second, form.second.size() <= 4);
			const auto report = parse(writer);
			const auto independent = independentVideoFactsError(report);
			QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
			QVERIFY2(!report.catalogue.files.first().length, qPrintable(context));
			QVERIFY2(report.problemNotes == QStringList({QStringLiteral("Object 201, OMFI:MDFL:Length: Numeric value has an unsupported type, incomplete width, byte order or conflicting values.")}),
					 qPrintable(context + ": " + report.problemNotes.join('\n')));
		}
	// Identical, correctly typed repetitions remain usable.
	auto writer = lengthFixture(big);
	writer.add(201, "OMFI:MDFL:Length", "omfi:Long", valid, true);
	const auto report = parse(writer);
	const auto independent = independentVideoFactsError(report);
	QVERIFY2(independent.isEmpty(), qPrintable(independent));
	QCOMPARE(report.catalogue.files.first().length, std::optional<qint64>(250));
	QVERIFY(report.problemNotes.isEmpty());
}

void TestBellaMdb::localDamageKeepsIndependentFacts_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("short length") << 0;
	QTest::newRow("zero rate denominator") << 1;
	QTest::newRow("short bit depth") << 2;
	QTest::newRow("conflicting length") << 3;
	QTest::newRow("wrong complete numeric type for component width") << 4;
}

void TestBellaMdb::localDamageKeepsIndependentFacts()
{
	QFETCH(int, variation);
	auto writer = video();
	if (variation == 0)
		replace(writer, 201, "OMFI:MDFL:Length", QByteArray::fromHex("010203"));
	else if (variation == 1)
		replace(writer, 201, "OMFI:MDFL:SampleRate", number<qint32>(25) + number<qint32>(0));
	else if (variation == 2)
		replace(writer, 201, "OMFI:CDCI:ComponentWidth", QByteArray::fromHex("0a00"));
	else if (variation == 3)
		writer.add(201, "OMFI:MDFL:Length", "omfi:Long", number<qint32>(500), true);
	else
		replace(writer, 201, "OMFI:CDCI:ComponentWidth", number<quint16>(10), "omfi:Ushort");
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.files.size(), 1);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.ownDescriptorQualified);
	QVERIFY(report.catalogue.fileReferencesComplete);
	QCOMPARE(bytes(facts.fileMobId), expectedUid(11));
	QCOMPARE(facts.kind, Bella::MdbKind::Video);
	QVERIFY(!report.problemNotes.isEmpty());
	if (variation == 0 || variation == 3)
		QVERIFY(!facts.length);
	else
		QCOMPARE(facts.length, std::optional<qint64>(250));
	QCOMPARE(facts.sampleRate.has_value(), variation != 1);
	QCOMPARE(facts.bitDepth.bits.has_value(), variation != 2 && variation != 4);
	QCOMPARE(report.catalogue.masters.first().clipName, QStringLiteral("Master"));
}

void TestBellaMdb::pcmaWrongNumericTypeDoesNotInventDepth()
{
	auto writer = audio("PCMA");
	rate(writer, 201, "OMFI:PCMA:AudioSamplingRate", 48000, 1);
	writer.add(201, "OMFI:MDAU:BitsPerSample", "omfi:Long", number<qint32>(24), true);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(facts.kind, Bella::MdbKind::Audio);
	QVERIFY(!facts.bitDepth.bits);
	QVERIFY(facts.audioFrequency);
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::duplicateOwnersRemainSeparate()
{
	auto writer = video();
	members(writer, {101, 102, 301});
	object(writer, 102, "MOBJ");
	writer.add(102, "OMFI:MOBJ:MobID", "omfi:UID", uid(11));
	writer.add(102, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(202, 1));
	object(writer, 202, "CDCI");
	rate(writer, 202, "OMFI:MDFL:SampleRate", 24, 1);
	writer.add(202, "OMFI:MDFL:Length", "omfi:Long", number<qint32>(500), true);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.files.size(), 2);
	QSet<qint64> lengths;
	for (const auto &facts : report.catalogue.files)
	{
		QCOMPARE(bytes(facts.fileMobId), expectedUid(11));
		QVERIFY(facts.length);
		lengths.insert(*facts.length);
	}
	QCOMPARE(lengths, QSet<qint64>({250, 500}));
}

void TestBellaMdb::masterOwnerLinksRemainSeparate()
{
	auto writer = video();
	members(writer, {101, 301, 302});
	master(writer, 302, 402, 502, 22, 11, "Other owner");
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.masters.size(), 2);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.masterLinksComplete);
	QCOMPARE(facts.directMasterRecords.size(), 2);
	QSet<qsizetype> visited;
	QSet<QString> names;
	for (const auto index : facts.directMasterRecords)
	{
		QVERIFY(index >= 0 && index < report.catalogue.masters.size());
		QVERIFY(!visited.contains(index));
		visited.insert(index);
		const auto &clip = report.catalogue.masters[index];
		QCOMPARE(bytes(clip.masterMobId), expectedUid(22));
		names.insert(clip.clipName);
	}
	QCOMPARE(names, QSet<QString>({QStringLiteral("Master"), QStringLiteral("Other owner")}));
}

void TestBellaMdb::incompleteMasterGraphDoesNotProveUnique()
{
	auto writer = video();
	members(writer, {101, 301, 302});
	master(writer, 302, 402, 502, 23, 11, "Unresolved owner");
	replace(writer, 502, "OMFI:SCLP:SourceID", QByteArray::fromHex("2a0000"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.directMasterRecords.size(), 1);
	QVERIFY(!facts.masterLinksComplete);
	QVERIFY(report.catalogue.fileReferencesComplete);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::missingTrackComponentDoesNotProveUnique()
{
	auto writer = video();
	members(writer, {101, 301, 302});
	master(writer, 302, 402, 502, 23, 11, "Unresolved owner");
	const auto property = writer.properties.value("OMFI:TRAK:TrackComponent");
	writer.entries.removeIf([property](const auto &entry)
							{ return entry.object == 402 && entry.property == property; });
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(facts.directMasterRecords.size(), 1);
	QVERIFY(!facts.masterLinksComplete);
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.length, std::optional<qint64>(250));
	QVERIFY(report.catalogue.fileReferencesComplete);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::unknownDictionaryCannotProveCodingAbsence()
{
	auto writer = video();
	attributes(writer, 101, 17000, {{"_PJ", "Independent Project"}});
	writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:GUID", QByteArray(16, '\0'));
	const auto property = writer.properties.take("OMFI:DIDD:EssenceCompression");
	// Its used dictionary definition is independently framed but lacks NUL.
	// Do not recover the intended name from these malformed bytes.
	writer.entries.append({property, 24, 21, QByteArray("OMFI:DIDD:EssenceCompression")});
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.files.size(), 1);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(bytes(facts.fileMobId), expectedUid(11));
	QCOMPARE(facts.compression.codingState, Bella::MdbCodingState::Unavailable);
	QCOMPARE(facts.project, QStringLiteral("Independent Project"));
	QCOMPARE(report.catalogue.masters.first().clipName, QStringLiteral("Master"));
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::codingByteOrderLookup_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::codingByteOrderLookup()
{
	QFETCH(bool, big);
	const std::array<const char *, 12> cases{{"complete GUID", "absent GUID", "short GUID", "conflicting GUIDs", "unsupported GUID type",
											  "matching local order", "opposite local order", "invalid local order width", "conflicting local order",
											  "identical GUIDs", "zero GUID is present", "unsupported local order type"}};
	for (int variation = 0; variation < int(cases.size()); ++variation)
	{
		const auto context = QString::fromLatin1(cases[std::size_t(variation)]);
		const auto report = parse(codingLookupFixture(big, variation));
		QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
		QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::Verified);
		QCOMPARE(report.catalogue.files.size(), 1);
		QCOMPARE(report.catalogue.masters.size(), 1);
		const auto &facts = report.catalogue.files.first();
		QCOMPARE(bytes(facts.fileMobId), expectedUid(11));
		QCOMPARE(facts.project, QStringLiteral("Independent Project"));
		QCOMPARE(bytes(report.catalogue.masters.first().masterMobId), expectedUid(22));
		QCOMPARE(report.catalogue.masters.first().clipName, QStringLiteral("Master"));
		QCOMPARE(report.catalogue.masters.first().type, Bella::MdbType::Media);
		const bool qualified = variation != 6 && variation != 7 && variation != 8 && variation != 11;
		QCOMPARE(facts.ownDescriptorQualified, qualified);
		if (qualified)
		{
			const auto independent = independentVideoFactsError(report);
			QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
			QCOMPARE(facts.length, std::optional<qint64>(250));
		}
		else
		{
			QVERIFY(!facts.length && !facts.sampleRate && !facts.resolution && !facts.bitDepth.bits);
			QCOMPARE(facts.numbering, Bella::MdbNumbering::Unavailable);
			QVERIFY(!report.catalogue.fileReferencesComplete && !facts.masterLinksComplete && facts.directMasterRecords.isEmpty());
		}
		const bool present = variation == 0 || variation == 5 || variation == 9 || variation == 10;
		QCOMPARE(facts.compression.codingState, present ? Bella::MdbCodingState::Present : variation == 1 ? Bella::MdbCodingState::Absent
																										  : Bella::MdbCodingState::Unavailable);
		if (present)
			QCOMPARE(QByteArray(reinterpret_cast<const char *>(facts.compression.codingLabel.data()), 16),
					 variation == 10 ? QByteArray(16, '\0') : QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10"));
		QStringList notes;
		if (!qualified)
			notes.append(QStringLiteral("Object 101, OMFI:MOBJ:PhysicalMedia: File ID is retained, but its own descriptor/context cannot be qualified."));
		else if (variation == 2 || variation == 3 || variation == 4)
			notes.append(QStringLiteral("Object 201, OMFI:DIDD:EssenceCompression: Coding identity is unavailable; it is not established absent."));
		QVERIFY2(report.problemNotes == notes, qPrintable(context + ": " + report.problemNotes.join('\n')));
	}
}

void TestBellaMdb::optionalIndexIdentity_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("agreeing comparable UIDs") << 0;
	QTest::newRow("source index UID disagrees") << 1;
	QTest::newRow("composition index UID disagrees") << 2;
}

void TestBellaMdb::optionalIndexIdentity()
{
	QFETCH(int, variation);
	auto writer = video();
	writer.mobIndex(1, "OMFI:SourceMobs", {{101, uid(variation == 1 ? 99 : 11)}});
	writer.mobIndex(1, "OMFI:CompositionMobs", {{301, uid(variation == 2 ? 99 : 22)}});
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QCOMPARE(facts->ownDescriptorQualified, variation != 1);
	if (variation == 1)
	{
		QVERIFY(!facts->length);
		QVERIFY(!report.catalogue.fileReferencesComplete);
	}
	else
		QCOMPARE(facts->length, std::optional<qint64>(250));
	if (variation == 2)
	{
		QVERIFY(report.catalogue.masters.isEmpty());
		QVERIFY(!facts->masterLinksComplete);
		QVERIFY(report.catalogue.fileReferencesComplete);
	}
	if (variation)
		QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::projectSelection_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<int>("selection");
	QTest::addColumn<int>("key");
	using Selection = Bella::MdbProjectSelection;
	using Key = Bella::MdbProjectKey;
	QTest::newRow("missing") << 0 << QString() << int(Selection::Unavailable) << int(Key::None);
	QTest::newRow("MacRoman own file") << 1 << QString::fromUtf8("zTeßt") << int(Selection::OwnFile) << int(Key::Pj);
	QTest::newRow("blank PJ permits PROJNAME") << 2 << QStringLiteral("Second") << int(Selection::OwnFile) << int(Key::ProjName);
	QTest::newRow("conflicting PJ blocks lower priority") << 3 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("identical duplicates agree") << 4 << QStringLiteral("Same") << int(Selection::OwnFile) << int(Key::Pj);
	QTest::newRow("marked Kind4 Unicode") << 5 << QString::fromUtf8("Zoë 🌊") << int(Selection::OwnFile) << int(Key::Pj);
	QTest::newRow("bad Kind4 UTF8 permits next key") << 6 << QStringLiteral("Second") << int(Selection::OwnFile) << int(Key::ProjName);
	QTest::newRow("local USER PJ") << 7 << QStringLiteral("Nested") << int(Selection::OwnFile) << int(Key::UserPj);
	QTest::newRow("exact keys only") << 8 << QStringLiteral("Second") << int(Selection::OwnFile) << int(Key::ProjName);
	QTest::newRow("immediate source fallback") << 9 << QStringLiteral("Source") << int(Selection::ImmediateSources) << int(Key::Pj);
	QTest::newRow("own file wins over different source") << 10 << QStringLiteral("Own") << int(Selection::OwnFile) << int(Key::Pj);
	QTest::newRow("conflicting immediate sources") << 11 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("agreeing sources mixed keys") << 12 << QStringLiteral("Source") << int(Selection::ImmediateSources) << int(Key::Mixed);
	QTest::newRow("incomplete source relationship") << 13 << QString() << int(Selection::Unavailable) << int(Key::None);
	QTest::newRow("conflicting values on same PJ attribute") << 14 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("USER target has wrong owner class") << 15 << QString() << int(Selection::Unavailable) << int(Key::None);
	QTest::newRow("conflicting values on same USER PJ attribute") << 16 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("conflicting PROJNAME") << 17 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("bad PJ and conflicting PROJNAME are independent") << 18 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("bad PJ and bad PROJNAME are independent") << 19 << QString() << int(Selection::Unavailable) << int(Key::None);
	QTest::newRow("conflicting immediate-source PJ retains separate owner explanations") << 20 << QString() << int(Selection::Conflict) << int(Key::None);
	QTest::newRow("unreadable immediate-source list retains separate owner explanations") << 21 << QString() << int(Selection::Unavailable) << int(Key::None);
}

void TestBellaMdb::projectSelection()
{
	QFETCH(int, variation);
	QFETCH(QString, expected);
	QFETCH(int, selection);
	QFETCH(int, key);
	auto writer = video();
	if (variation == 1)
		attributes(writer, 101, 17000, {{"_PJ", QByteArray::fromHex("7a5465a774")}});
	else if (variation == 2)
		attributes(writer, 101, 17000, {{"_PJ", " \t "}, {"PROJNAME", "Second"}});
	else if (variation == 3)
		attributes(writer, 101, 17000, {{"_PJ", "First"}, {"_PJ", "Different"}, {"PROJNAME", "Must not win"}});
	else if (variation == 4)
		attributes(writer, 101, 17000, {{"_PJ", "Same"}, {"_PJ", "Same"}});
	else if (variation == 5)
		attributes(writer, 101, 17000, {{"_PJ", QByteArray(2, '\0') + QString::fromUtf8("Zoë 🌊").toUtf8(), 4}});
	else if (variation == 6)
		attributes(writer, 101, 17000, {{"_PJ", QByteArray::fromHex("0000c328"), 4}, {"PROJNAME", "Second"}});
	else if (variation == 7)
	{
		attributes(writer, 101, 17000, {{"_USER", {}, 3, 18800}});
		attributes(writer, 0, 18800, {{"_PJ", "Nested"}});
	}
	else if (variation == 8)
		attributes(writer, 101, 17000, {{"_PJ_suffix", "Must not win"}, {"PROJNAME", "Second"}});
	else if (variation == 14)
	{
		attributes(writer, 101, 17000, {{"_PJ", "First"}, {"PROJNAME", "Must not win"}});
		writer.add(17001, "OMFI:ATTB:StringAttribute", "omfi:String", QByteArray("Different\0", 10));
	}
	else if (variation == 15 || variation == 16)
	{
		attributes(writer, 101, 17000, {{"_USER", {}, 3, 18800}});
		attributes(writer, 0, 18800, {{"_PJ", "Nested"}});
		if (variation == 15)
			replace(writer, 18800, "OMFI:ObjID", QByteArray("MOBJ"));
		else
			writer.add(18801, "OMFI:ATTB:StringAttribute", "omfi:String", QByteArray("Different\0", 10));
	}
	else if (variation == 17)
		attributes(writer, 101, 17000, {{"PROJNAME", "First"}, {"PROJNAME", "Different"}});
	else if (variation == 18)
		attributes(writer, 101, 17000, {{"_PJ", QByteArray::fromHex("0000c328"), 4}, {"PROJNAME", "First"}, {"PROJNAME", "Different"}});
	else if (variation == 19)
		attributes(writer, 101, 17000, {{"_PJ", QByteArray::fromHex("0000c328"), 4}, {"PROJNAME", QByteArray::fromHex("0000c328"), 4}});
	else if (variation == 20 || variation == 21)
	{
		members(writer, {101, 301, 601});
		source(writer, 601, 33, {{"_PJ", "First"}, {"_PJ", "Different"}});
		immediateSources(writer, {33});
		if (variation == 21)
			replace(writer, 20000 + 601 * 16, "OMFI:ObjID", QByteArray("MOBJ"));
	}
	else if (variation >= 9)
	{
		members(writer, variation == 11 || variation == 12 ? QVector<quint32>{101, 301, 601, 602}
														   : QVector<quint32>{101, 301, 601});
		source(writer, 601, 33, {{"_PJ", "Source"}});
		immediateSources(writer, variation >= 11 ? QVector<quint32>{33, 44} : QVector<quint32>{33});
		if (variation == 10)
			attributes(writer, 101, 17000, {{"_PJ", "Own"}});
		if (variation == 11)
			source(writer, 602, 44, {{"_PJ", "Different"}});
		if (variation == 12)
			source(writer, 602, 44, {{"PROJNAME", "Source"}});
	}
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QCOMPARE(facts->project, expected);
	QCOMPARE(int(facts->projectSelection), selection);
	QCOMPARE(int(facts->projectKey), key);
	QCOMPARE(facts->length, std::optional<qint64>(250));
	if (variation == 3 || variation == 6 || variation == 11 || variation >= 13)
		QVERIFY(!report.problemNotes.isEmpty());
	if (variation == 3 || variation == 14)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, _PJ: Project names disagree; Project is left blank without substituting another value.")}));
	if (variation == 17)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, PROJNAME: Project names disagree; Project is left blank without substituting another value.")}));
	if (variation == 18)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, _PJ: Project text is unreadable, unsupported or conflicting."),
												   QStringLiteral("Object 101, PROJNAME: Project names disagree; Project is left blank without substituting another value.")}));
	if (variation == 19)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, _PJ: Project text is unreadable, unsupported or conflicting."),
												   QStringLiteral("Object 101, PROJNAME: Project text is unreadable, unsupported or conflicting.")}));
	if (variation == 16)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, OMFI:CPNT:Attributes: Own Project candidates conflict; lower-priority Project substitution is blocked.")}));
	if (variation == 11)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 101, OMFI:SCLP:SourceID: Immediate-source Project candidates conflict; Project is withheld.")}));
	if (variation == 20)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 601, _PJ: Project text is unreadable, unsupported or conflicting."),
												   QStringLiteral("Object 101, OMFI:SCLP:SourceID: Immediate-source Project candidates conflict; Project is withheld.")}));
	if (variation == 21)
		QCOMPARE(report.problemNotes, QStringList({QStringLiteral("Object 601, _PJ: Project text is unreadable, unsupported or conflicting."),
												   QStringLiteral("Object 601, PROJNAME: Project text is unreadable, unsupported or conflicting."),
												   QStringLiteral("Object 101, OMFI:SCLP:SourceID: Immediate-source Project relationships or attributes are incomplete; Project is withheld.")}));
}

void TestBellaMdb::unreadableProjectListKeepsProblems_data()
{
	QTest::addColumn<int>("listIssue");
	QTest::newRow("wrong owner attribute-list class") << 0;
	QTest::newRow("wrong attribute-entry class") << 1;
	QTest::newRow("conflicting owner attribute-list pointers") << 2;
}

void TestBellaMdb::unreadableProjectListKeepsProblems()
{
	QFETCH(int, listIssue);
	auto writer = video();
	attributes(writer, 101, 17000, {{"_PJ", "Must not win"}, {"PROJNAME", "Also unavailable"}});
	if (listIssue == 2)
	{
		attributes(writer, 0, 18000, {});
		writer.add(101, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(18000, 1));
	}
	else
		replace(writer, listIssue == 0 ? 17000 : 17001, "OMFI:ObjID", QByteArray("MOBJ"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QVERIFY(facts->project.isEmpty());
	QCOMPARE(facts->projectSelection, listIssue == 2 ? Bella::MdbProjectSelection::Conflict : Bella::MdbProjectSelection::Unavailable);
	QCOMPARE(facts->projectKey, Bella::MdbProjectKey::None);
	QVERIFY(facts->ownDescriptorQualified);
	QCOMPARE(facts->length, std::optional<qint64>(250));
	// One unreadable owner list has one explanation with its Project consequence.
	QCOMPARE(report.problemNotes, QStringList({listIssue == 2 ? QStringLiteral("Object 101, OMFI:CPNT:Attributes: Project attributes conflict; Project is left blank without substituting a linked record.") : QStringLiteral("Object 101, OMFI:CPNT:Attributes: Project attributes cannot be read; Project is left blank without substituting a linked record.")}));
}

void TestBellaMdb::originalBinUtf8Fallback_data()
{
	QTest::addColumn<bool>("validUnicode");
	QTest::newRow("valid Unicode preferred") << true;
	QTest::newRow("invalid Unicode falls back to legacy") << false;
}

void TestBellaMdb::originalBinUtf8Fallback()
{
	QFETCH(bool, validUnicode);
	auto writer = video();
	attributes(writer, 301, 17000, {{"_ORG_BIN", {}, 3, 18100}});
	object(writer, 18100, "MCBR");
	writer.add(18100, "OMFI:MCBR:MC:binName", "omfi:String", QByteArray::fromHex("4c6567616379a700"));
	writer.add(18100, "OMFI:MCBR:MC:binNameUTF8", "omfi:String",
			   validUnicode ? QString::fromUtf8("Bïn").toUtf8() + '\0' : QByteArray::fromHex("c32800"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(report.catalogue.masters.first().originalBin,
			 validUnicode ? QString::fromUtf8("Bïn") : QString::fromUtf8("Legacyß"));
	QCOMPARE(report.catalogue.masters.first().clipName, QStringLiteral("Master"));
	if (!validUnicode)
		QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::originalBinConflictBoundary_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::originalBinConflictBoundary()
{
	QFETCH(bool, big);
	const std::array<const char *, 16> cases{{"conflicting Unicode with legacy", "conflicting Unicode without legacy", "identical Unicode duplicates",
											  "invalid Unicode uses separate legacy", "Unicode wins over different legacy", "Unicode wins over conflicting legacy",
											  "missing Unicode uses legacy", "blank Unicode uses legacy", "conflicting legacy", "identical legacy duplicates",
											  "distinct bin references with equal names", "same bin reference repeated", "distinct bin references with different names",
											  "one attribute has conflicting references", "blank and nonblank Unicode conflict", "usable and unreadable Unicode duplicates"}};
	for (int variation = 0; variation < int(cases.size()); ++variation)
		for (const bool reverse : {false, true})
		{
			const auto context = QStringLiteral("%1, %2 entry order").arg(QString::fromLatin1(cases[std::size_t(variation)])).arg(reverse ? QStringLiteral("reverse") : QStringLiteral("forward"));
			auto writer = lengthFixture(big);
			QVector<Attribute> refs{{"_ORG_BIN", {}, 3, variation == 13 && reverse ? 18200u : 18100u}};
			if (variation == 10 || variation == 11 || variation == 12)
				refs.append({"_ORG_BIN", {}, 3, variation == 11 ? 18100u : 18200u});
			if (reverse)
				std::reverse(refs.begin(), refs.end());
			attributes(writer, 301, 18000, refs);
			object(writer, 18100, "MCBR");
			QVector<QByteArray> modern{"Unicode"}, legacy{"Legacy"};
			if (variation == 0 || variation == 1)
				modern.append("Conflicting");
			else if (variation == 2)
				modern.append("Unicode");
			else if (variation == 3)
				modern = {QByteArray::fromHex("c328")};
			else if (variation == 6 || variation == 8 || variation == 9)
				modern.clear();
			else if (variation == 7)
				modern = {" \t "};
			else if (variation == 14)
				modern.append("");
			else if (variation == 15)
				modern.append(QByteArray::fromHex("c328"));
			if (variation == 1)
				legacy.clear();
			else if (variation == 5 || variation == 8)
				legacy.append("Different legacy");
			else if (variation == 9)
				legacy.append("Legacy");
			if (reverse)
			{
				std::reverse(modern.begin(), modern.end());
				std::reverse(legacy.begin(), legacy.end());
			}
			for (const auto &value : modern)
				writer.add(18100, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", value + '\0');
			for (const auto &value : legacy)
				writer.add(18100, "OMFI:MCBR:MC:binName", "omfi:String", value + '\0');
			if (variation == 10 || variation == 12 || variation == 13)
			{
				object(writer, 18200, "MCBR");
				writer.add(18200, "OMFI:MCBR:MC:binNameUTF8", "omfi:String",
						   QByteArray(variation == 12 ? "Different" : "Unicode") + '\0');
			}
			if (variation == 13)
				writer.add(18001, "OMFI:ATTB:ObjAttribute", "omfi:ObjRef", writer.reference(reverse ? 18100 : 18200, 1));
			QString expected = QStringLiteral("Unicode");
			QStringList notes;
			if (variation == 0 || variation == 1 || variation == 14)
			{
				expected.clear();
				notes.append(QStringLiteral("Object 18100, OMFI:MCBR:MC:binNameUTF8: Conflicting Unicode bin names; bin is unavailable."));
			}
			else if (variation == 3 || variation == 6 || variation == 7 || variation == 9 || variation == 15)
			{
				expected = QStringLiteral("Legacy");
				if (variation == 3 || variation == 15)
					notes.append(QStringLiteral("Object 18100, OMFI:MCBR:MC:binNameUTF8: Invalid Unicode bin name; trying its separate legacy counterpart."));
			}
			else if (variation == 8 || variation == 12 || variation == 13)
			{
				expected.clear();
				notes.append(variation == 8 ? QStringLiteral("Object 18100, OMFI:MCBR:MC:binName: Legacy bin counterpart is unreadable or conflicting.") : variation == 12 ? QStringLiteral("Object 301, _ORG_BIN: Conflicting direct original-bin names; bin is unavailable.")
																																										   : QStringLiteral("Object 18001, _ORG_BIN: Original-bin attribute is not a readable object reference."));
			}
			const auto report = parse(writer);
			const auto independent = independentVideoFactsError(report);
			QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
			QCOMPARE(report.catalogue.files.first().length, std::optional<qint64>(250));
			QVERIFY2(report.catalogue.masters.first().originalBin == expected, qPrintable(context));
			QVERIFY2(report.problemNotes == notes, qPrintable(context + ": " + report.problemNotes.join('\n')));
		}
}

void TestBellaMdb::audioSourceRepresentations_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("PCMA original unreduced fraction") << 0;
	QTest::newRow("WAVE unsigned integer") << 1;
	QTest::newRow("WAVE full unsigned32 frequency") << 2;
	QTest::newRow("AIFF fixed inner byte order") << 3;
	QTest::newRow("AIFF-C copied summary") << 4;
	QTest::newRow("AIFF full significand in ordinary audio range") << 5;
}

void TestBellaMdb::audioSourceRepresentations()
{
	QFETCH(int, variation);
	const QByteArray cls = variation == 0 ? "PCMA" : variation <= 2 ? "WAVD"
																	: "AIFD";
	// Outer object order is BE; WAVE stays LE and AIFF stays BE internally.
	auto writer = audio(cls, true);
	if (variation == 0)
	{
		rate(writer, 201, "OMFI:PCMA:AudioSamplingRate", 96000, 2);
		writer.add(201, "OMFI:MDAU:BitsPerSample", "omfi:Ushort", number<quint16>(24, true), true);
	}
	else if (variation <= 2)
		writer.add(201, "OMFI:WAVD:Summary", "omfi:VarLenBytes",
				   waveSummary(variation == 2 ? std::numeric_limits<quint32>::max() : 48000, variation == 2 ? 8 : 24));
	else
		writer.add(201, "OMFI:AIFD:Summary", "omfi:VarLenBytes",
				   aiffSummary(QByteArray::fromHex(variation == 5 ? "400ebb80000000000001" : "400ebb80000000000000"), variation == 4));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(facts.kind, Bella::MdbKind::Audio);
	QVERIFY(facts.audioFrequency);
	QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(variation == 2 ? 8 : 24));
	if (variation == 4)
		QCOMPARE(facts.bitDepth.storageBits, std::optional<quint32>(24));
	QVERIFY(facts.sampleRate);
	QCOMPARE(facts.sampleRate->words.numerator, 48000);
	QVERIFY(facts.audioEditRate);
	QCOMPARE(facts.audioEditRate->words.numerator, 25);
	if (variation == 0)
	{
		const auto *original = std::get_if<Bella::MdbRateWords>(&*facts.audioFrequency);
		QVERIFY(original);
		QCOMPARE(original->numerator, 96000);
		QCOMPARE(original->denominator, 2);
	}
	else if (variation <= 2)
	{
		const auto *original = std::get_if<quint32>(&*facts.audioFrequency);
		QVERIFY(original);
		QCOMPARE(*original, variation == 2 ? std::numeric_limits<quint32>::max() : quint32(48000));
	}
	else
	{
		const auto *original = std::get_if<std::array<quint8, 10>>(&*facts.audioFrequency);
		QVERIFY(original);
		QCOMPARE(QByteArray(reinterpret_cast<const char *>(original->data()), 10),
				 QByteArray::fromHex(variation == 5 ? "400ebb80000000000001" : "400ebb80000000000000"));
	}
}

void TestBellaMdb::unsupportedWaveStorage_data()
{
	QTest::addColumn<bool>("extensible");
	QTest::newRow("unknown standard format has no qualified storage width") << false;
	QTest::newRow("unknown extensible subtype retains container width") << true;
}

void TestBellaMdb::unsupportedWaveStorage()
{
	QFETCH(bool, extensible);
	auto writer = audio("WAVD");
	QByteArray format = number<quint16>(extensible ? 0xfffe : 2) + number<quint16>(1) +
						number<quint32>(48000) + number<quint32>(144000) + number<quint16>(3) + number<quint16>(24);
	if (extensible)
		format += number<quint16>(22) + number<quint16>(24) + number<quint32>(0) +
				  QByteArray::fromHex("7856341200001000800000aa00389b71");
	const auto fields = chunk("fmt ", format, false) + "data" + number<quint32>(6000000);
	const auto summary = QByteArray("RIFF") + number<quint32>(6000000 + quint32(fields.size()) + 4) + "WAVE" + fields;
	writer.add(201, "OMFI:WAVD:Summary", "omfi:VarLenBytes", summary);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.audioFrequency);
	const auto *frequency = std::get_if<quint32>(&*facts.audioFrequency);
	QVERIFY(frequency);
	QCOMPARE(*frequency, quint32(48000));
	QVERIFY(!facts.bitDepth.bits);
	QCOMPARE(facts.bitDepth.storageBits, extensible ? std::optional<quint32>(24) : std::nullopt);
	QCOMPARE(facts.compression.audioEncoding, Bella::MdbAudioEncoding::Unknown);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::zeroWaveWidthProblem_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("PCM zero width") << 0;
	QTest::newRow("zero frequency is an independent failure") << 1;
	QTest::newRow("float zero width") << 2;
	QTest::newRow("extensible PCM zero width") << 3;
	QTest::newRow("incomplete extensible format is an independent failure") << 4;
	QTest::newRow("unsupported extensible subtype is an independent failure") << 5;
}

void TestBellaMdb::zeroWaveWidthProblem()
{
	QFETCH(int, variation);
	const quint16 encoding = variation >= 3 ? 0xfffe : variation == 2 ? 3
																	  : 1;
	const quint32 frequency = variation == 1 ? 0 : 48000;
	QByteArray format = number(encoding) + number<quint16>(1) + number(frequency) +
						number<quint32>(0) + number<quint16>(0) + number<quint16>(0);
	if (variation == 3 || variation == 5)
		format += number<quint16>(22) + number<quint16>(24) + number<quint32>(0) +
				  QByteArray::fromHex(variation == 5 ? "7856341200001000800000aa00389b71" : "0100000000001000800000aa00389b71");
	const auto fields = chunk("fmt ", format, false);
	const auto summary = QByteArray("RIFF") + number<quint32>(quint32(fields.size()) + 4) + "WAVE" + fields;
	auto writer = audio("WAVD");
	writer.add(201, "OMFI:WAVD:Summary", "omfi:VarLenBytes", summary);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	QVERIFY(facts.sampleRate);
	QCOMPARE(facts.audioFrequency.has_value(), frequency != 0);
	if (facts.audioFrequency)
	{
		const auto *value = std::get_if<quint32>(&*facts.audioFrequency);
		QVERIFY(value);
		QCOMPARE(*value, frequency);
	}
	QVERIFY(!facts.bitDepth.bits);
	QVERIFY(!facts.bitDepth.storageBits);
	QCOMPARE(facts.bitDepth.representation, variation >= 4 ? Bella::MdbDepthRepresentation::Unknown : variation == 2 ? Bella::MdbDepthRepresentation::Float
																													 : Bella::MdbDepthRepresentation::Integer);
	QCOMPARE(facts.compression.audioEncoding, variation >= 4 ? Bella::MdbAudioEncoding::Unknown : variation == 2 ? Bella::MdbAudioEncoding::IeeeFloat
																												 : Bella::MdbAudioEncoding::Pcm);
	const auto context = QStringLiteral("Object 201, OMFI:WAVD:Summary: ");
	QStringList expected;
	if (variation == 1)
		expected.append(context + QStringLiteral("WAVE fmt sample frequency is zero."));
	expected.append(context + QStringLiteral("Audio bit depth is unavailable because the recorded sample storage width is zero."));
	if (variation == 4)
		expected.append(context + QStringLiteral("WAVE extensible format has no complete extension-size field; precision and encoding are unavailable."));
	if (variation == 5)
		expected.append(context + QStringLiteral("WAVE extensible subtype is unsupported; storage width does not establish sample precision."));
	QCOMPARE(report.problemNotes, expected);
}

void TestBellaMdb::aiffInvalidFrequencyIsLocal_data()
{
	QTest::addColumn<QByteArray>("frequency");
	QTest::newRow("negative") << QByteArray::fromHex("c00ebb80000000000000");
	QTest::newRow("zero") << QByteArray(10, '\0');
	QTest::newRow("infinity") << QByteArray::fromHex("7fff8000000000000000");
}

void TestBellaMdb::aiffInvalidFrequencyIsLocal()
{
	QFETCH(QByteArray, frequency);
	auto writer = audio("AIFD");
	writer.add(201, "OMFI:AIFD:Summary", "omfi:VarLenBytes", aiffSummary(frequency, false));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(!facts.audioFrequency);
	QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(24));
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	QVERIFY(facts.sampleRate);
	QVERIFY(facts.ownDescriptorQualified);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::aiffBoundedFractionEdges_data()
{
	QTest::addColumn<QByteArray>("frequency");
	QTest::addColumn<bool>("usable");
	// These are authored mathematical boundary controls, not found audio files.
	QTest::newRow("one Hz") << QByteArray::fromHex("3fff8000000000000000") << true;
	QTest::newRow("one Hz plus full64 significand bit") << QByteArray::fromHex("3fff8000000000000001") << true;
	QTest::newRow("unsigned64 largest numerator") << QByteArray::fromHex("403effffffffffffffff") << true;
	QTest::newRow("numerator needs65 bits") << QByteArray::fromHex("403f8000000000000000") << false;
	QTest::newRow("half Hz exact") << QByteArray::fromHex("3ffe8000000000000000") << true;
	QTest::newRow("half Hz denominator needs65 bits") << QByteArray::fromHex("3ffe8000000000000001") << false;
	QTest::newRow("denominator2pow63 fits") << QByteArray::fromHex("3fc08000000000000000") << true;
	QTest::newRow("denominator2pow64 does not fit") << QByteArray::fromHex("3fbf8000000000000000") << false;
}

void TestBellaMdb::aiffBoundedFractionEdges()
{
	QFETCH(QByteArray, frequency);
	QFETCH(bool, usable);
	auto writer = audio("AIFD");
	writer.add(201, "OMFI:AIFD:Summary", "omfi:VarLenBytes", aiffSummary(frequency, false));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(facts.audioFrequency.has_value(), usable);
	QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(24));
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	if (usable)
	{
		const auto *original = std::get_if<std::array<quint8, 10>>(&*facts.audioFrequency);
		QVERIFY(original);
		QCOMPARE(QByteArray(reinterpret_cast<const char *>(original->data()), 10), frequency);
	}
	else
		QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::audioSummaryDamageBoundaries_data()
{
	QTest::addColumn<bool>("aiff");
	QTest::addColumn<int>("variation");
	for (const bool aiff : {false, true})
	{
		const auto prefix = aiff ? QStringLiteral("AIFF ") : QStringLiteral("WAVE ");
		QTest::newRow(qPrintable(prefix + "later unrelated broken chunk")) << aiff << 0;
		QTest::newRow(qPrintable(prefix + "required format itself short")) << aiff << 1;
		QTest::newRow(qPrintable(prefix + "frequency conflict only")) << aiff << 2;
		QTest::newRow(qPrintable(prefix + "depth conflict only")) << aiff << 3;
		QTest::newRow(qPrintable(prefix + "third format cannot repair frequency conflict")) << aiff << 4;
		QTest::newRow(qPrintable(prefix + "third format cannot repair depth conflict")) << aiff << 5;
		QTest::newRow(qPrintable(prefix + "later formats cannot supply unavailable first frequency")) << aiff << 6;
		QTest::newRow(qPrintable(prefix + "later formats cannot supply unavailable first depth")) << aiff << 7;
		QTest::newRow(qPrintable(prefix + "short first prefix then complete formats")) << aiff << 8;
		QTest::newRow(qPrintable(prefix + "third format cannot repair encoding conflict")) << aiff << 9;
	}
}

void TestBellaMdb::audioSummaryDamageBoundaries()
{
	QFETCH(bool, aiff);
	QFETCH(int, variation);
	const auto makeFormat = [aiff, variation](bool otherRate, quint16 bits, bool floating = false)
	{
		if (aiff)
		{
			auto common = number<qint16>(1, true) + number<quint32>(2000000, true) + number<qint16>(qint16(bits), true) +
						  QByteArray::fromHex(otherRate ? "400eac44000000000000" : "400ebb80000000000000");
			if (variation == 9)
				common += QByteArray(floating ? "fl32\0\0" : "in32\0\0", 6);
			return common;
		}
		const quint32 frequency = otherRate ? 44100 : 48000;
		const quint16 alignment = quint16(bits / 8);
		return number<quint16>(floating ? 3 : 1) + number<quint16>(1) + number(frequency) +
			   number<quint32>(frequency * alignment) + number(alignment) + number(bits);
	};
	auto first = makeFormat(false, variation == 7 ? 0 : variation == 9 ? 32
																	   : 24,
							variation == 9);
	if (variation == 1 || variation == 8)
		first.chop(1);
	else if (variation == 6)
		first.replace(aiff ? 8 : 4, aiff ? 10 : 4, QByteArray(aiff ? 10 : 4, '\0'));
	const QByteArray formatId = aiff ? "COMM" : "fmt ";
	auto fields = chunk(formatId, first, aiff);
	if (variation == 0)
		fields += QByteArray("JUNK") + number<quint32>(100, aiff) + QByteArray("xx");
	else if (variation >= 2)
	{
		fields += chunk(formatId, makeFormat(variation == 2 || variation == 4, variation == 3 || variation == 5 ? 16 : variation == 9 ? 32
																																	  : 24),
						aiff);
		if (variation >= 4)
			fields += chunk(formatId, makeFormat(false, variation == 9 ? 32 : 24, variation == 9), aiff);
	}
	const auto summary = QByteArray(aiff ? "FORM" : "RIFF") + number<quint32>(quint32(fields.size()) + 4, aiff) +
						 QByteArray(aiff ? variation == 9 ? "AIFC" : "AIFF" : "WAVE") + fields;
	auto writer = audio(aiff ? "AIFD" : "WAVD");
	writer.add(201, aiff ? "OMFI:AIFD:Summary" : "OMFI:WAVD:Summary", "omfi:VarLenBytes", summary);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	QVERIFY(facts.sampleRate);
	const bool frequencyUsable = variation == 0 || variation == 3 || variation == 5 || variation == 7 || variation == 9;
	const bool depthUsable = variation == 0 || variation == 2 || variation == 4 || variation == 6 || variation == 9;
	QCOMPARE(facts.audioFrequency.has_value(), frequencyUsable);
	QCOMPARE(facts.bitDepth.bits.has_value(), depthUsable);
	if (facts.bitDepth.bits)
		QCOMPARE(*facts.bitDepth.bits, variation == 9 ? quint32(32) : quint32(24));
	QCOMPARE(facts.bitDepth.storageBits.has_value(), aiff ? variation == 9 : depthUsable);
	if (facts.bitDepth.storageBits)
		QCOMPARE(*facts.bitDepth.storageBits, variation == 9 ? quint32(32) : quint32(24));
	const bool encodingUsable = variation != 1 && variation != 8 && variation != 9;
	QCOMPARE(facts.bitDepth.representation, encodingUsable ? Bella::MdbDepthRepresentation::Integer : Bella::MdbDepthRepresentation::Unknown);
	QCOMPARE(facts.compression.audioEncoding, encodingUsable ? Bella::MdbAudioEncoding::Pcm : Bella::MdbAudioEncoding::Unknown);
	if (variation == 4)
		QCOMPARE(report.problemNotes.filter(QStringLiteral("Repeated audio format chunks disagree on sample frequency.")).size(), 1);
	if (variation == 5)
	{
		QCOMPARE(report.problemNotes.filter(QStringLiteral("Repeated audio format chunks disagree on sample precision.")).size(), 1);
		QCOMPARE(report.problemNotes.filter(QStringLiteral("Repeated audio format chunks disagree on sample storage width.")).size(), aiff ? 0 : 1);
	}
	if (variation == 9)
	{
		QCOMPARE(report.problemNotes.filter(QStringLiteral("Repeated audio format chunks disagree on sample representation.")).size(), 1);
		QCOMPARE(report.problemNotes.filter(QStringLiteral("Repeated audio format chunks disagree on audio encoding.")).size(), 1);
	}
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::audioSummaryFramingProblems_data()
{
	QTest::addColumn<bool>("aiff");
	QTest::addColumn<int>("earlierFormat");
	QTest::addColumn<int>("fault");
	for (const bool aiff : {false, true})
	{
		const auto prefix = aiff ? QStringLiteral("AIFF ") : QStringLiteral("WAVE ");
		for (const bool earlier : {false, true})
		{
			const auto position = earlier ? QStringLiteral("after format ") : QStringLiteral("before format ");
			QTest::newRow(qPrintable(prefix + position + "incomplete chunk header")) << aiff << int(earlier) << 0;
			QTest::newRow(qPrintable(prefix + position + "unrelated chunk exceeds range")) << aiff << int(earlier) << 1;
			QTest::newRow(qPrintable(prefix + position + "required chunk exceeds range")) << aiff << int(earlier) << 2;
		}
		QTest::newRow(qPrintable(prefix + "no format before sound data")) << aiff << 0 << 3;
		QTest::newRow(qPrintable(prefix + "short required prefix then broken framing")) << aiff << 2 << 1;
	}
}

void TestBellaMdb::audioSummaryFramingProblems()
{
	QFETCH(bool, aiff);
	QFETCH(int, earlierFormat);
	QFETCH(int, fault);
	const QByteArray formatId = aiff ? "COMM" : "fmt ";
	QByteArray format;
	if (aiff)
		format = number<qint16>(1, true) + number<quint32>(2000000, true) + number<qint16>(24, true) +
				 QByteArray::fromHex("400ebb80000000000000");
	else
		format = number<quint16>(1) + number<quint16>(1) + number<quint32>(48000) +
				 number<quint32>(144000) + number<quint16>(3) + number<quint16>(24);
	if (earlierFormat == 2)
		format.chop(1);
	QByteArray fields = earlierFormat ? chunk(formatId, format, aiff) : QByteArray{};
	if (fault == 0)
		fields += "JUNK";
	else if (fault == 1 || fault == 2)
		fields += (fault == 2 ? formatId : QByteArray("JUNK")) + number<quint32>(100, aiff) + QByteArray("xx");
	else
		fields += chunk("JUNK", QByteArray("xx"), aiff) +
				  (aiff ? QByteArray("SSND") : QByteArray("data")) + number<quint32>(0, aiff);
	const auto summary = QByteArray(aiff ? "FORM" : "RIFF") + number<quint32>(quint32(fields.size()) + 4, aiff) +
						 QByteArray(aiff ? "AIFF" : "WAVE") + fields;
	auto writer = audio(aiff ? "AIFD" : "WAVD");
	writer.add(201, aiff ? "OMFI:AIFD:Summary" : "OMFI:WAVD:Summary", "omfi:VarLenBytes", summary);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(facts.ownDescriptorQualified);
	QCOMPARE(facts.length, std::optional<qint64>(96000));
	QVERIFY(facts.sampleRate);
	const bool usable = earlierFormat == 1 && fault != 2;
	QCOMPARE(facts.audioFrequency.has_value(), usable);
	QCOMPARE(facts.bitDepth.bits, usable ? std::optional<quint32>(24) : std::nullopt);
	QCOMPARE(facts.bitDepth.storageBits, usable && !aiff ? std::optional<quint32>(24) : std::nullopt);
	QCOMPARE(facts.bitDepth.representation, usable ? Bella::MdbDepthRepresentation::Integer : Bella::MdbDepthRepresentation::Unknown);
	QCOMPARE(facts.compression.audioEncoding, usable ? Bella::MdbAudioEncoding::Pcm : Bella::MdbAudioEncoding::Unknown);
	const auto context = aiff ? QStringLiteral("Object 201, OMFI:AIFD:Summary: ") : QStringLiteral("Object 201, OMFI:WAVD:Summary: ");
	QStringList expected;
	if (earlierFormat == 2)
		expected.append(context + QStringLiteral("Audio Summary format chunk has an incomplete required prefix."));
	if (fault == 3)
		expected.append(context + QStringLiteral("Audio Summary has no complete required format chunk before sound data."));
	else
	{
		auto reason = fault == 0 ? QStringLiteral("Audio Summary has an incomplete metadata chunk header.") : QStringLiteral("Audio Summary metadata chunk exceeds its available container range.");
		if (!earlierFormat)
			reason += QStringLiteral(" No complete required format chunk could be read before sound data.");
		expected.append(context + reason);
	}
	QCOMPARE(report.problemNotes, expected);
}

void TestBellaMdb::specialDepthCodes_data()
{
	QTest::addColumn<QByteArray>("label");
	QTest::addColumn<int>("code");
	QTest::addColumn<quint32>("bits");
	QTest::addColumn<int>("representation");
	using Representation = Bella::MdbDepthRepresentation;
	QTest::newRow("DNx half float") << QByteArray::fromHex("060e2b340401010d0401020203070100") << 253 << quint32(16) << int(Representation::HalfFloat);
	QTest::newRow("DNx float") << QByteArray::fromHex("060e2b340401010d0401020203070100") << 254 << quint32(32) << int(Representation::Float);
	QTest::newRow("DNx fixed") << QByteArray::fromHex("060e2b340401010d0401020203070200") << 254 << quint32(16) << int(Representation::S2_14Fixed);
}

void TestBellaMdb::specialDepthCodes()
{
	QFETCH(QByteArray, label);
	QFETCH(int, code);
	QFETCH(quint32, bits);
	QFETCH(int, representation);
	auto writer = video();
	replace(writer, 201, "OMFI:CDCI:ComponentWidth", number<qint32>(code));
	writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:GUID", codingWire(label, false));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(bits));
	QCOMPARE(facts.bitDepth.rawComponentWidth, std::optional<qint32>(code));
	QCOMPARE(int(facts.bitDepth.representation), representation);
	QCOMPARE(facts.compression.codingState, Bella::MdbCodingState::Present);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(facts.compression.codingLabel.data()), 16), label);
}

void TestBellaMdb::unrelatedCodingDoesNotValidateDepthSentinel()
{
	auto writer = video();
	replace(writer, 201, "OMFI:CDCI:ComponentWidth", number<qint32>(253));
	writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:GUID",
			   codingWire(QByteArray::fromHex("060e2b34040101010401020201011100"), false));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(!facts.bitDepth.bits);
	QCOMPARE(facts.bitDepth.rawComponentWidth, std::optional<qint32>(253));
	QCOMPARE(facts.bitDepth.representation, Bella::MdbDepthRepresentation::Unknown);
	QVERIFY(facts.length);
}

void TestBellaMdb::rgbaKeepsSeparateArrays()
{
	auto writer = video();
	replace(writer, 201, "OMFI:ObjID", QByteArray("RGBA"));
	writer.add(201, "OMFI:RGBA:PixelLayout", "omfi:VarLenBytes", QByteArray("A"));
	writer.add(201, "OMFI:RGBA:PixelStructure", "omfi:VarLenBytes", QByteArray(1, char(16)));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &depth = report.catalogue.files.first().bitDepth;
	QVERIFY(depth.pixelLayout);
	QVERIFY(depth.pixelStructure);
	QCOMPARE(depth.pixelLayout->count, quint8(1));
	QCOMPARE(depth.pixelStructure->count, quint8(1));
	QCOMPARE(depth.pixelLayout->values[0], quint8('A'));
	QCOMPARE(depth.pixelStructure->values[0], quint8(16));
	QCOMPARE(depth.bits, std::optional<quint32>(16));
}

void TestBellaMdb::rgbaMismatchedArraysDoNotGuessDepth()
{
	auto writer = video();
	replace(writer, 201, "OMFI:ObjID", QByteArray("RGBA"));
	writer.add(201, "OMFI:RGBA:PixelLayout", "omfi:VarLenBytes", QByteArray("A"));
	writer.add(201, "OMFI:RGBA:PixelStructure", "omfi:VarLenBytes", QByteArray::fromHex("0808"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto &facts = report.catalogue.files.first();
	QVERIFY(!facts.bitDepth.bits);
	QVERIFY(facts.ownDescriptorQualified);
	QVERIFY(facts.length);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::numbering_data()
{
	QTest::addColumn<quint32>("flags");
	QTest::addColumn<bool>("indirect");
	for (const bool indirect : {false, true})
		for (quint32 flags : {0u, 1u, 2u, 3u, 0x80000000u, 0x80000001u})
			QTest::newRow(qPrintable(QStringLiteral("%1 flags %2").arg(indirect ? "immediate source" : "own timecode").arg(flags))) << flags << indirect;
}

void TestBellaMdb::numbering()
{
	QFETCH(quint32, flags);
	QFETCH(bool, indirect);
	auto writer = video();
	timecode(writer, flags, indirect);
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QCOMPARE(facts->numbering, (flags & 1) ? Bella::MdbNumbering::RecordedDf : Bella::MdbNumbering::RecordedNdf);
	QCOMPARE(facts->length, std::optional<qint64>(250));
	QVERIFY(facts->sampleRate);
	QCOMPARE(facts->sampleRate->words.numerator, 2997);
	QCOMPARE(facts->sampleRate->words.denominator, 100);
}

void TestBellaMdb::numberingDamageKeepsCountAndClock()
{
	auto writer = video();
	timecode(writer, 1, true);
	replace(writer, 624, "OMFI:TCCP:Flags", QByteArray::fromHex("0100"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QCOMPARE(facts->numbering, Bella::MdbNumbering::Unavailable);
	QCOMPARE(facts->length, std::optional<qint64>(250));
	QVERIFY(facts->sampleRate);
	QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMdb::knownNdfClockDoesNotNeedDamagedFlag()
{
	auto writer = video();
	timecode(writer, 1, true);
	replace(writer, 201, "OMFI:MDFL:SampleRate", number<qint32>(25) + number<qint32>(1));
	replace(writer, 624, "OMFI:TCCP:Flags", QByteArray::fromHex("01"));
	const auto report = parse(writer);
	QCOMPARE(report.reading, Bella::MdbReadResult::Completed);
	const auto *facts = file(report, expectedUid(11));
	QVERIFY(facts);
	QCOMPARE(facts->numbering, Bella::MdbNumbering::NotRequired);
	QCOMPARE(facts->length, std::optional<qint64>(250));
}

void TestBellaMdb::optionalDetailsDoNotRemoveType()
{
	auto writer = video();
	replace(writer, 301, "OMFI:MOBJ:UsageCode", number<quint32>(1));
	const auto withoutDetails = parse(writer, false);
	const auto withDetails = parse(writer, true);
	QCOMPARE(withoutDetails.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(withDetails.reading, Bella::MdbReadResult::Completed);
	QCOMPARE(withoutDetails.catalogue.masters.first().type, Bella::MdbType::Precompute);
	QCOMPARE(withDetails.catalogue.masters.first().type, Bella::MdbType::Precompute);
	QCOMPARE(withoutDetails.catalogue.masters.first().clipName, QStringLiteral("Master"));
	QCOMPARE(withoutDetails.catalogue.masters.first().precomputeCategory, Bella::MdbPrecomputeCategory::Unknown);
	QCOMPARE(withDetails.catalogue.masters.first().precomputeCategory, Bella::MdbPrecomputeCategory::RenderedEffects);
	QCOMPARE(withoutDetails.catalogue.files.first().directMasterRecords, withDetails.catalogue.files.first().directMasterRecords);
}

void TestBellaMdb::precomputeImportKindBoundary_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::precomputeImportKindBoundary()
{
	QFETCH(bool, big);
	const std::array<const char *, 24> cases{{"single integer", "single text", "single BOB", "unsupported Kind0", "unsupported Kind99", "unreadable Kind",
											  "wrong Kind type", "wrong Kind width", "conflicting Kind", "multiple identical text entries", "mixed integer and text",
											  "mixed text and object", "null object with two video tracks", "non-null object with two video tracks",
											  "object with one video track", "object with no video tracks", "absent attribute", "object with unreadable track kind",
											  "object with unreadable reference", "multiple object entries", "single text with unreadable tracks",
											  "multiple differing text entries", "object with missing payload", "object with missing tracks"}};
	for (int variation = 0; variation < int(cases.size()); ++variation)
		for (const bool reverse : {false, true})
		{
			const auto context = QStringLiteral("%1, %2 entry order").arg(QString::fromLatin1(cases[std::size_t(variation)])).arg(reverse ? QStringLiteral("reverse") : QStringLiteral("forward"));
			auto writer = lengthFixture(big);
			replace(writer, 301, "OMFI:MOBJ:UsageCode", number<quint32>(1, big));
			const Attribute integer{"_IMPORTSETTING", {}, 1, 0, 42};
			const Attribute text{"_IMPORTSETTING", "Readable text"};
			const Attribute bob{"_IMPORTSETTING", QByteArray(2, '\0') + "Readable BOB", 4};
			const Attribute objectValue{"_IMPORTSETTING", {}, 3, variation == 13 ? 18800u : 0u};
			QVector<Attribute> imports;
			if (variation == 0)
				imports = {integer};
			else if (variation == 2)
				imports = {bob};
			else if (variation == 3 || variation == 4)
				imports = {{"_IMPORTSETTING", {}, quint16(variation == 3 ? 0 : 99)}};
			else if (variation == 8)
				imports = {reverse ? text : objectValue};
			else if (variation == 9 || variation == 21)
				imports = {text, {"_IMPORTSETTING", variation == 9 ? QByteArray("Readable text") : QByteArray("Different text")}};
			else if (variation == 10)
				imports = {integer, text};
			else if (variation == 11)
				imports = {text, objectValue};
			else if (variation == 19)
				imports = {objectValue, objectValue};
			else if (variation == 1 || variation == 5 || variation == 6 || variation == 7 || variation == 20)
				imports = {text};
			else if (variation != 16)
				imports = {objectValue};
			if (reverse && variation != 8)
				std::reverse(imports.begin(), imports.end());
			if (!imports.isEmpty())
				attributes(writer, 301, 18000, imports);
			if (variation == 5)
				replace(writer, 18001, "OMFI:ATTB:Kind", QByteArray(1, char(2)));
			else if (variation == 6)
				replace(writer, 18001, "OMFI:ATTB:Kind", number<quint16>(2, big), "omfi:Ushort");
			else if (variation == 7)
				replace(writer, 18001, "OMFI:ATTB:Kind", number<qint32>(2, big));
			else if (variation == 8)
				writer.add(18001, "OMFI:ATTB:Kind", "omfi:AttrKind", number<quint16>(reverse ? 3 : 2, big), true);
			if (variation == 13)
				object(writer, 18800, "ATTR");
			if ((variation >= 12 && variation <= 19) || variation == 22 || variation == 23)
			{
				writer.add(501, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<qint16>(variation == 15 ? 2 : 1, big), true);
				if (variation != 14 && variation != 15 && variation != 16)
				{
					replace(writer, 301, "OMFI:TRKG:Tracks", number<quint16>(2, big) + writer.reference(401, 1) + writer.reference(402, 1));
					object(writer, 402, "TRAK");
					writer.add(402, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(502, 1));
					object(writer, 502, "SCLP");
					writer.add(502, "OMFI:CPNT:TrackKind", "omfi:TrackType", number<qint16>(1, big), true);
					writer.add(502, "OMFI:SCLP:SourceID", "omfi:UID", uid(11, big));
				}
			}
			if (variation == 17)
				replace(writer, 501, "OMFI:CPNT:TrackKind", QByteArray(1, char(1)));
			else if (variation == 18)
				replace(writer, 18001, "OMFI:ATTB:ObjAttribute", QByteArray(3, '\0'));
			else if (variation == 20)
				replace(writer, 301, "OMFI:TRKG:Tracks", QByteArray(1, '\0'));
			else if (variation == 22 || variation == 23)
			{
				const quint32 owner = variation == 22 ? 18001 : 301;
				const auto property = writer.properties.value(variation == 22 ? "OMFI:ATTB:ObjAttribute" : "OMFI:TRKG:Tracks");
				writer.entries.erase(std::remove_if(writer.entries.begin(), writer.entries.end(), [owner, property](const auto &entry)
													{ return entry.object == owner && entry.property == property; }),
									 writer.entries.end());
			}
			Bella::MdbPrecomputeCategory expected = Bella::MdbPrecomputeCategory::Unknown;
			if (variation <= 2 || variation == 14 || variation == 15 || variation == 16 || variation == 20)
				expected = Bella::MdbPrecomputeCategory::RenderedEffects;
			else if (variation == 12 || variation == 13 || variation == 19)
				expected = Bella::MdbPrecomputeCategory::TitlesAndMatteKeys;
			QStringList notes;
			if (variation == 20)
				notes.append(QStringLiteral("Object 301, OMFI:TRKG:Tracks: Component references are unreadable or conflicting; dependent relationships are incomplete."));
			else if (variation == 23)
				notes.append(QStringLiteral("Object 301, component graph: Required component link is missing."));
			for (const bool details : {false, true})
			{
				const auto report = parse(writer, details);
				const auto independent = independentVideoFactsError(report, Bella::MdbType::Precompute, variation != 20 && variation != 23);
				QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
				QCOMPARE(report.catalogue.files.first().length, std::optional<qint64>(250));
				QVERIFY(report.catalogue.masters.first().originalBin.isEmpty());
				QVERIFY2(report.catalogue.masters.first().precomputeCategory == (details ? expected : Bella::MdbPrecomputeCategory::Unknown), qPrintable(context));
				QVERIFY2(report.problemNotes == notes, qPrintable(context + ": " + report.problemNotes.join('\n')));
			}
		}
}

void TestBellaMdb::sharedMasterAttributes_data()
{
	QTest::addColumn<bool>("big");
	QTest::newRow("metadata LE") << false;
	QTest::newRow("metadata BE") << true;
}

void TestBellaMdb::sharedMasterAttributes()
{
	QFETCH(bool, big);
	const std::array<const char *, 11> cases{{"missing list", "explicit empty list", "empty ATTR without AttrRefs", "wrong list class",
											  "wrong entry class", "conflicting list references", "name warning before list warning", "shared usable Bin and Import",
											  "unreadable optional object payload", "unreadable optional track kind", "name warning before Unicode fallback"}};
	for (int variation = 0; variation < int(cases.size()); ++variation)
		for (const bool details : {false, true})
		{
			const auto context = QString::fromLatin1(cases[std::size_t(variation)]);
			const auto report = parse(sharedMasterAttributesFixture(big, variation), details);
			const auto name = variation == 6 || variation == 10 ? QString{} : QStringLiteral("Master");
			const auto independent = independentVideoFactsError(report, Bella::MdbType::Precompute, true, name);
			QVERIFY2(independent.isEmpty(), qPrintable(context + ": " + independent));
			QCOMPARE(report.catalogue.files.first().length, std::optional<qint64>(250));
			QCOMPARE(report.catalogue.masters.first().originalBin, variation == 10 ? QStringLiteral("Legacy") : variation >= 7 ? QStringLiteral("Bin")
																															   : QString{});
			const auto category = !details ? Bella::MdbPrecomputeCategory::Unknown : variation <= 2					 ? Bella::MdbPrecomputeCategory::RenderedEffects
																				 : variation == 7 || variation == 10 ? Bella::MdbPrecomputeCategory::TitlesAndMatteKeys
																													 : Bella::MdbPrecomputeCategory::Unknown;
			QCOMPARE(report.catalogue.masters.first().precomputeCategory, category);
			QStringList notes;
			if (variation == 6 || variation == 10)
				notes.append(QStringLiteral("Object 301, OMFI:CPNT:Name: Master name is unreadable or conflicting."));
			if (variation >= 3 && variation <= 6)
				notes.append(QStringLiteral("Object 301, _ORG_BIN: Direct original-bin attributes cannot be established."));
			else if (variation == 10)
				notes.append(QStringLiteral("Object 18100, OMFI:MCBR:MC:binNameUTF8: Invalid Unicode bin name; trying its separate legacy counterpart."));
			QVERIFY2(report.problemNotes == notes, qPrintable(context + ": " + report.problemNotes.join('\n')));
		}
}

void TestBellaMdb::genuineDatabases_data()
{
	QTest::addColumn<QString>("relative");
	for (const auto *path : {"msmMMOB.mdb", "msmMMOB_macroman.mdb", "corpus_headers/msmMMOB.mdb",
							 "corpus_headers/msmMMOB_round3.mdb", "omf/avid_supporting/msmMMOB.mdb", "omf/mc2026_audio/msmMMOB.mdb"})
		QTest::newRow(path) << QString::fromLatin1(path);
}

void TestBellaMdb::genuineDatabases()
{
	QFETCH(QString, relative);
	const std::atomic_bool stop{false};
	const auto report = Bella::parseMdb(QStringLiteral(FIXTURES_DIR) + '/' + relative, stop, true);
	QVERIFY2(report.reading == Bella::MdbReadResult::Completed, qPrintable(report.problemNotes.join('\n')));
	QCOMPARE(report.sizeDateCheck, Bella::MdbCheckResult::Verified);
	QVERIFY(!report.catalogue.files.isEmpty());
	QVERIFY(!report.catalogue.masters.isEmpty());
	for (const auto &facts : report.catalogue.files)
		for (const auto index : facts.directMasterRecords)
			QVERIFY(index >= 0 && index < report.catalogue.masters.size());
}

void TestBellaMdb::genuineAudioFieldsAndIdentities()
{
	const std::atomic_bool stop{false};
	const auto report = Bella::parseMdb(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/msmMMOB.mdb"), stop, true);
	QVERIFY2(report.reading == Bella::MdbReadResult::Completed, qPrintable(report.problemNotes.join('\n')));
	QCOMPARE(report.catalogue.files.size(), 2);
	// Full identities were independently pinned against genuine PMR bytes.
	const QMap<QByteArray, QByteArray> expectedMasters{
		{QByteArray::fromHex("060a2b340101010101010f00130000007429976a70397047060e2b347f7f2a80"),
		 QByteArray::fromHex("060a2b340101010101010f00130000007429976a4e397047060e2b347f7f2a80")},
		{QByteArray::fromHex("060a2b340101010101010f00130000009729976a3ec57047060e2b347f7f2a80"),
		 QByteArray::fromHex("060a2b340101010101010f00130000009729976a3dc57047060e2b347f7f2a80")}};
	QSet<QByteArray> seen;
	for (const auto &facts : report.catalogue.files)
	{
		const auto identity = bytes(facts.fileMobId);
		QVERIFY(expectedMasters.contains(identity));
		QVERIFY(!seen.contains(identity));
		seen.insert(identity);
		QCOMPARE(facts.kind, Bella::MdbKind::Audio);
		QVERIFY(facts.ownDescriptorQualified);
		QCOMPARE(facts.bitDepth.bits, std::optional<quint32>(24));
		QVERIFY(facts.audioFrequency);
		if (const auto *frequency = std::get_if<quint32>(&*facts.audioFrequency))
			QCOMPARE(*frequency, quint32(48000));
		else
		{
			const auto *extended = std::get_if<std::array<quint8, 10>>(&*facts.audioFrequency);
			QVERIFY(extended);
			QCOMPARE(QByteArray(reinterpret_cast<const char *>(extended->data()), 10), QByteArray::fromHex("400ebb80000000000000"));
		}
		QVERIFY(facts.masterLinksComplete);
		QCOMPARE(facts.directMasterRecords.size(), 1);
		const auto index = facts.directMasterRecords.first();
		QVERIFY(index >= 0 && index < report.catalogue.masters.size());
		QCOMPARE(bytes(report.catalogue.masters[index].masterMobId), expectedMasters.value(identity));
	}
}

QTEST_GUILESS_MAIN(TestBellaMdb)
#include "tst_bellamdb.moc"
