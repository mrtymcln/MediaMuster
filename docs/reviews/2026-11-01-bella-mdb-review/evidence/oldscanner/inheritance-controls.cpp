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

int main(){
 for(bool big : {false,true}) for(int mode=0;mode<7;++mode){
  auto w=video(big);
  if(mode==1) replace(w,401,"OMFI:ObjID","FILL");
  if(mode==2) replace(w,401,"OMFI:ObjID","TCCP");
  if(mode==3 || mode==4){
   replace(w,501,"OMFI:SCLP:SourceID",QByteArray(12,'\0'));
   object(w,601,"SCLP");
   w.add(601,"OMFI:SCLP:SourceID","omfi:UID",uid(11,big));
   w.referenceArray(301,mode==3 ? "OMFI:SEQU:Sequence" : "OMFI:NEST:Slots",{601},1);
  }
  if(mode>=5){
   source(w,701,33,{{"_PJ","Unexpected-route Project"}});
   members(w,{101,301,701});
   object(w,702,"SCLP");
   w.add(702,"OMFI:SCLP:SourceID","omfi:UID",uid(33,big));
   if(mode==5) w.referenceArray(101,"OMFI:SEQU:Sequence",{702},1);
   if(mode==6){
    object(w,703,"TRAK");
    w.add(703,"OMFI:TRAK:TrackComponent","omfi:ObjRef",w.reference(702,1));
    w.referenceArray(101,"OMFI:TRKG:Tracks",{703},1);
   }
  }
  QFile f(QStringLiteral("/tmp/bella-mdb-inheritance-controls/graph-%1-%2.mdb").arg(big ? "BE" : "LE").arg(mode));
  if(!f.open(QIODevice::WriteOnly)||f.write(w.build())<0) return 1;
 }

for (bool big : {false, true}) for (int mode=0;mode<7;++mode) {
 auto w=video(big);
 replace(w,301,"OMFI:MOBJ:UsageCode",number<quint32>(1,big));
 if(mode<=1) {
  attributes(w,301,18000,{});
  if(mode==1) replace(w,18000,"OMFI:ATTR:AttrRefs",number<quint16>(1,big)+w.reference(0,1));
 } else {
  attributes(w,301,18000,{{"_IMPORTSETTING",{},3,0}});
  if(mode==2) replace(w,18000,"OMFI:ATTR:AttrRefs",number<quint16>(2,big)+w.reference(18001,1)+w.reference(0,1));
  replace(w,301,"OMFI:TRKG:Tracks",number<quint16>(2,big)+w.reference(401,1)+w.reference(402,1));
  object(w,402,"TRAK");
  w.add(402,"OMFI:TRAK:TrackComponent","omfi:ObjRef",w.reference(502,1));
  object(w,502,"SCLP");
  w.add(502,"OMFI:SCLP:SourceID","omfi:UID",uid(11,big));
  for(quint32 c : {501u,502u}) w.add(c,"OMFI:CPNT:TrackKind","omfi:TrackType",number<quint16>(1,big),true);
  if(mode==3) replace(w,401,"OMFI:ObjID","ATTB");
  if(mode==4) replace(w,501,"OMFI:ObjID","ATTB");
  if(mode==5) replace(w,18001,"OMFI:ATTB:Name","_IMPORTSETTING");
  if(mode==6) replace(w,18001,"OMFI:ATTB:Name",QByteArray("_IMPORTSETTING\0garbage",22));
 }
 QFile f(QStringLiteral("/tmp/bella-mdb-inheritance-controls/category-%1-%2.mdb").arg(big ? "BE" : "LE").arg(mode));
 if(!f.open(QIODevice::WriteOnly)||f.write(w.build())<0) return 1;
}
}
