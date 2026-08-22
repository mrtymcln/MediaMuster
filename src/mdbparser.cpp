#include "mdbparser.h"
#include "bentofile.h"
#include "logging.h"
#include "mobid.h"

#include <QByteArrayView>
#include <QFile>
#include <QSet>
#include <QVector>

// Reads msmMMOB.mdb through its Bento table of contents.
//
// MARK: - What the database is
//
// `msmMMOB.mdb` is an OMF Interchange 2.x object store (Avid's pre-AAF
// format; the spec is public) inside an Apple Bento container (BentoFile).
// Every clip and every essence file is an OMFI:MOBJ object with a 32-byte
// OMFI:MOBJ:MobID — the same UMID the PMR and the MXF carry, in Avid's byte
// order (PMR↔MDB join raw; MXF needs MobId::toPmrForm). Properties are named
// by a dictionary the file itself embeds, so this parser resolves
// "OMFI:CPNT:Name" per file and hard-codes no ids.
//
// The objects worth reading, and where their facts live:
//
//   master mob   (no PhysicalMedia; UsageCode 7, or 1 for a precompute)
//     OMFI:CPNT:Name                 the clip name Avid displays
//     OMFI:CPNT:Attributes → ATTR → OMFI:ATTR:AttrRefs → ATTB list, each
//        OMFI:ATTB:Name + OMFI:ATTB:Kind (1 int, 2 string, 3 object, 4 bob):
//          _ORG_BIN       (3) → MCBR → OMFI:MCBR:MC:binNameUTF8 (else MC:binName)
//          _IMPORTSETTING (3) → ATTR → nested _SRCFILE (3) → MACL →
//                               OMFI:FL:POSIXPathName (else PathNameUTF8, PathName)
//          _USER          (3) → ATTR → `Video` (2) = container ("QTFF"),
//                               `UNC Path` (2) = the same path again
//   file mob     (OMFI:MOBJ:PhysicalMedia → a media descriptor; UsageCode 0,
//                 or 9 for a precompute)
//     OMFI:CPNT:EditRate             the clip's frame rate (audio duration maths)
//     descriptor class               CDCI/MPGI/RGBA/JPED video, PCMA/MPGA/WAVE audio
//     OMFI:DIDD:EssenceCompression   the 16-byte codec label — in AAF AUID byte
//                                    order (see auidToUl); or, on legacy DNxHD,
//     OMFI:DIDD:DIDResolutionID      Avid's numeric resolution id (see ulFromResId)
//     OMFI:DIDD:StoredWidth/Height   + OMFI:DIDD:FrameLayout (u16): the MDB stores
//                                    a HALF height for layouts 1 AND 3 (the MXF only
//                                    for 1) — normalised here, flagged for finalise
//     OMFI:MDFL:SampleRate           rational; decimal approximations (2997/100),
//                                    so it goes through MxfParser::applyEditRate
//     OMFI:MDFL:Length               frames (video) / samples (audio)
//     OMFI:CDCI:ComponentWidth / OMFI:MDAU:BitsPerSample / OMFI:MDAU:NumChannels
//     OMFI:TRKG:Tracks → TRAK → TrackComponent → SEQU/SCLP/TCCP: drop frame
//                                    from OMFI:TCCP:Flags, reached through the
//                                    source mobs a SCLP points at
//   source mob   (PhysicalMedia → MDES; the import/tape source) — not needed:
//                 the master's attributes carry the source path.
//
// Verified 2026-08-19..22 against 360 whole MXF files with their own
// databases plus 795 archived headers across two database generations: clip
// name 360/360, project, kind, codec label 119/119 (byte-identical after the
// AUID reorder), dims, fps, durations 119/119 + 241/241, bits, channels,
// source path 354/354, usage pairs 1,155/1,155. The one known gap is MPEG
// audio (MPGA), which carries no codec label in the MDB — such a file is
// reported essenceComplete=false and the scanner reads its header instead.
//
// Three traps that produced confidently wrong "it's not in there" readings
// before this parser existed, kept here so nobody re-learns them:
//  - The codec label is stored in GUID order; searched in MXF order it scores
//    0/41. Un-rotate it (auidToUl) and it is byte-identical on 104/104 files.
//  - A property NAME appears once in the whole file — in the dictionary.
//    Presence of values is only visible through the table of contents.
//  - The same MobID is written on more than one MOBJ object (the TONE clip's
//    name sits on one, its _ORG_BIN on another). Records must be MERGED across
//    duplicates, first non-empty per field; first-wins-per-MobID loses the bin.

namespace
{
	// MARK: - Property ids, resolved once per file

	struct Props
	{
		int mobId, usage, name, editRate, attrs, physMedia;
		int attrRefs, attbName, attbKind, attbInt, attbString, attbObj;
		int binNameUtf8, binName, posixPath, pathUtf8, pathName;
		int essComp, resId, width, height, layout, sampleRate, length, compWidth, bits, channels;
		int tracks, trackComp, sequence, sourceId, tcFlags;

		explicit Props(const BentoFile &b)
			: mobId(b.propertyId("OMFI:MOBJ:MobID")), usage(b.propertyId("OMFI:MOBJ:UsageCode")),
			  name(b.propertyId("OMFI:CPNT:Name")), editRate(b.propertyId("OMFI:CPNT:EditRate")),
			  attrs(b.propertyId("OMFI:CPNT:Attributes")), physMedia(b.propertyId("OMFI:MOBJ:PhysicalMedia")),
			  attrRefs(b.propertyId("OMFI:ATTR:AttrRefs")), attbName(b.propertyId("OMFI:ATTB:Name")),
			  attbKind(b.propertyId("OMFI:ATTB:Kind")), attbInt(b.propertyId("OMFI:ATTB:IntAttribute")),
			  attbString(b.propertyId("OMFI:ATTB:StringAttribute")),
			  attbObj(b.propertyId("OMFI:ATTB:ObjAttribute")),
			  binNameUtf8(b.propertyId("OMFI:MCBR:MC:binNameUTF8")), binName(b.propertyId("OMFI:MCBR:MC:binName")),
			  posixPath(b.propertyId("OMFI:FL:POSIXPathName")), pathUtf8(b.propertyId("OMFI:FL:PathNameUTF8")),
			  pathName(b.propertyId("OMFI:FL:PathName")), essComp(b.propertyId("OMFI:DIDD:EssenceCompression")),
			  resId(b.propertyId("OMFI:DIDD:DIDResolutionID")), width(b.propertyId("OMFI:DIDD:StoredWidth")),
			  height(b.propertyId("OMFI:DIDD:StoredHeight")), layout(b.propertyId("OMFI:DIDD:FrameLayout")),
			  sampleRate(b.propertyId("OMFI:MDFL:SampleRate")), length(b.propertyId("OMFI:MDFL:Length")),
			  compWidth(b.propertyId("OMFI:CDCI:ComponentWidth")), bits(b.propertyId("OMFI:MDAU:BitsPerSample")),
			  channels(b.propertyId("OMFI:MDAU:NumChannels")), tracks(b.propertyId("OMFI:TRKG:Tracks")),
			  trackComp(b.propertyId("OMFI:TRAK:TrackComponent")), sequence(b.propertyId("OMFI:SEQU:Sequence")),
			  sourceId(b.propertyId("OMFI:SCLP:SourceID")), tcFlags(b.propertyId("OMFI:TCCP:Flags"))
		{
		}
	};

	/// Avid's placeholder MOB — byte-identical across unrelated projects, in no
	/// MXF. Never a real clip; skipped on sight.
	const QByteArray &placeholderMob()
	{
		static const QByteArray kMob =
			QByteArray::fromHex("060a2b340101010001010f0013000000bacc8a260647d528e5cd3f5b5f40443f");
		return kMob;
	}

	bool isAudioClass(const QByteArray &cls)
	{
		return cls == "PCMA" || cls == "MPGA" || cls == "WAVE";
	}
	bool isMediaClass(const QByteArray &cls)
	{
		return isAudioClass(cls) || cls == "CDCI" || cls == "MPGI" || cls == "RGBA" || cls == "JPED";
	}

	// MARK: - Codec label

	/// OMFI:DIDD:EssenceCompression stores the SMPTE label as an AAF AUID: the
	/// first three GUID fields little-endian (u32, u16, u16) followed by the
	/// eight remaining bytes — and the eight remaining bytes are the label's
	/// FIRST half. So UL = bytes[8..16] + rev(bytes[0..4]) + rev(bytes[4..6]) +
	/// rev(bytes[6..8]). Measured byte-identical to MXF tag 0x3201 on every
	/// file that carries it.
	QByteArray auidToUl(QByteArrayView auid)
	{
		if (auid.size() != 16)
			return {};
		QByteArray ul;
		ul.reserve(16);
		ul.append(auid.data() + 8, 8);
		for (int i = 3; i >= 0; --i)
			ul.append(auid[i]);
		ul.append(auid[5]);
		ul.append(auid[4]);
		ul.append(auid[7]);
		ul.append(auid[6]);
		return ul;
	}

	/// Legacy DNxHD essence carries no EssenceCompression in the MDB, only
	/// Avid's numeric resolution id. The DNxHD labels are numbered in step
	/// with it: label byte 13 == id − 1234 (verified on all ten ids in the
	/// corpus: 1235→01 … 1253→13). Ids outside the DNxHD range (2400, 3416,
	/// 557…) are other codecs that DO carry a label, so the guard matters.
	QByteArray ulFromResId(quint32 resId)
	{
		if (resId <= 1234 || resId >= 1490)
			return {};
		QByteArray ul = QByteArray::fromHex("060e2b340401010a04010202");
		ul.append(char(0x71));
		ul.append(char(resId - 1234));
		ul.append(char(0));
		ul.append(char(0));
		return ul;
	}

	QString baseName(const QString &path)
	{
		const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
		return slash < 0 ? path : path.mid(slash + 1);
	}

	// MARK: - Attribute trees

	/// ATTR → AttrRefs → ATTB[]; nested ATTRs are followed only where a fact
	/// we want lives below them (_IMPORTSETTING, _USER). `seen` guards the
	/// shared nodes Avid writes.
	void walkAttributes(const BentoFile &b, const Props &p, quint32 attrObj, MdbMaster &m,
						QSet<quint32> &seen, int depth)
	{
		if (attrObj == 0 || depth > 4 || seen.contains(attrObj))
			return;
		seen.insert(attrObj);
		const QVector<quint32> attbs = BentoFile::handles(b.value(attrObj, p.attrRefs));
		for (quint32 attb : attbs)
		{
			const QString name = BentoFile::string(b.value(attb, p.attbName));
			const quint32 kind = BentoFile::uint(b.value(attb, p.attbKind));
			if (kind == 3)
			{
				const quint32 target = BentoFile::handle(b.value(attb, p.attbObj));
				if (target == 0)
					continue;
				const QByteArray cls = b.objectClass(target);
				if (name == QLatin1String("_ORG_BIN") && cls == "MCBR")
				{
					if (m.bin.isEmpty())
					{
						m.bin = BentoFile::utf8String(b.value(target, p.binNameUtf8));
						if (m.bin.isEmpty())
							m.bin = BentoFile::string(b.value(target, p.binName));
					}
				}
				else if (name == QLatin1String("_IMPORTSETTING"))
				{
					m.isImported = true;
					if (cls == "ATTR")
						walkAttributes(b, p, target, m, seen, depth + 1);
				}
				else if (name == QLatin1String("_SRCFILE") && cls == "MACL")
				{
					if (m.sourceFilePath.isEmpty())
					{
						m.sourceFilePath = BentoFile::string(b.value(target, p.posixPath));
						if (m.sourceFilePath.isEmpty())
							m.sourceFilePath = BentoFile::utf8String(b.value(target, p.pathUtf8));
						if (m.sourceFilePath.isEmpty())
							m.sourceFilePath = BentoFile::string(b.value(target, p.pathName));
					}
				}
				else if (name == QLatin1String("_USER") && cls == "ATTR")
				{
					walkAttributes(b, p, target, m, seen, depth + 1);
				}
			}
			else if (kind == 2)
			{
				if (name == QLatin1String("Video") || name == QLatin1String("Audio"))
				{
					if (m.sourceContainer.isEmpty())
						m.sourceContainer = BentoFile::string(b.value(attb, p.attbString));
				}
				else if (name == QLatin1String("UNC Path"))
				{
					if (m.sourceFilePath.isEmpty())
						m.sourceFilePath = BentoFile::string(b.value(attb, p.attbString));
				}
			}
		}
	}

	// MARK: - Track walk (drop frame)

	/// The first TCCP reachable from `mob`'s tracks: through SEQU components,
	/// and through SCLP source references into other mobs (the timecode lives
	/// on the tape/import source mob, not the file mob). 0 if none.
	quint32 findTimecodeComponent(const BentoFile &b, const Props &p, quint32 mob,
								  const QHash<QByteArray, quint32> &objectByMob, QSet<quint32> &seen,
								  int depth)
	{
		if (mob == 0 || depth > 6 || seen.contains(mob))
			return 0;
		seen.insert(mob);
		for (quint32 track : BentoFile::handles(b.value(mob, p.tracks)))
		{
			QVector<quint32> stack{BentoFile::handle(b.value(track, p.trackComp))};
			while (!stack.isEmpty())
			{
				const quint32 c = stack.takeLast();
				if (c == 0 || seen.contains(c))
					continue;
				seen.insert(c);
				const QByteArray cls = b.objectClass(c);
				if (cls == "TCCP")
					return c;
				if (cls == "SEQU")
					stack += BentoFile::handles(b.value(c, p.sequence));
				else if (cls == "SCLP")
				{
					const QByteArrayView src = b.value(c, p.sourceId);
					if (src.size() == MobId::kRawSize)
					{
						const quint32 srcMob = objectByMob.value(src.toByteArray(), 0);
						if (const quint32 t = findTimecodeComponent(b, p, srcMob, objectByMob, seen, depth + 1))
							return t;
					}
				}
			}
		}
		return 0;
	}

	// MARK: - Essence

	/// Fill `f.essence` from the descriptor the way a header parse would, then
	/// hand it to MxfParser::finalise so every derived value comes from the
	/// same code as the header path.
	void readEssence(const BentoFile &b, const Props &p, quint32 mobObj, quint32 desc,
					 const QHash<QByteArray, quint32> &objectByMob, MdbFile &f)
	{
		MxfMetadata &e = f.essence;
		const QByteArray cls = b.objectClass(desc);
		if (!isMediaClass(cls))
			return;
		e.isAudio = isAudioClass(cls);

		// Codec label: the stored AUID, else rebuilt from the resolution id.
		e.essenceContainerLabel = auidToUl(b.value(desc, p.essComp));
		if (e.essenceContainerLabel.isEmpty())
			e.essenceContainerLabel = ulFromResId(BentoFile::uint(b.value(desc, p.resId)));

		// Rate. Video goes through the same label matcher as tag 0x3001.
		qint32 num = 0, den = 0;
		if (BentoFile::rational(b.value(desc, p.sampleRate), num, den) && den > 0 && num > 0)
			MxfParser::applyEditRate(e, quint32(num), quint32(den));

		const quint32 length = BentoFile::uint(b.value(desc, p.length));
		if (e.isAudio)
		{
			// Length is samples. The frame count comes from the mob's own edit
			// rate; finalise then re-derives the timecode base from these two
			// exactly as it does for a header (frames × rate ÷ samples).
			e.descriptorDuration = length;
			qint32 erNum = 0, erDen = 0;
			if (BentoFile::rational(b.value(mobObj, p.editRate), erNum, erDen) && erDen > 0 && erNum > 0 &&
				length > 0 && e.sampleRate > 0)
				e.durationFrames = qRound(double(length) * erNum / erDen / e.sampleRate);
			if (const quint32 bits = BentoFile::uint(b.value(desc, p.bits)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
			e.channels = int(BentoFile::uint(b.value(desc, p.channels)));
		}
		else
		{
			e.durationFrames = length;
			e.width = int(BentoFile::uint(b.value(desc, p.width)));
			int height = int(BentoFile::uint(b.value(desc, p.height)));
			const QByteArrayView layoutV = b.value(desc, p.layout);
			const int layout = layoutV.isEmpty() ? -1 : int(BentoFile::uint(layoutV));
			// The MDB stores one FIELD height for layouts 1 and 3 (the MXF only
			// for 1). Normalise here and say so; finalise keeps its own
			// layout-1 rule for headers and the 1088/544 padding rule for both.
			if (height > 0 && (layout == 1 || layout == 3))
				height *= 2;
			e.height = height;
			e.frameLayout = layout;
			e.heightIsFrameHeight = height > 0;
			if (const quint32 bits = BentoFile::uint(b.value(desc, p.compWidth)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
		}

		// Drop frame: the timecode component is on a source mob, reached
		// through the file mob's SCLP references.
		QSet<quint32> seen;
		if (const quint32 tccp = findTimecodeComponent(b, p, mobObj, objectByMob, seen, 0))
			e.dropFrame = BentoFile::uint(b.value(tccp, p.tcFlags)) != 0;

		MxfParser::finalise(e);

		// Complete = the scanner may skip the header. Audio needs a codec it
		// can name (PCM is the no-label default; MPEG audio has no label here
		// and would wrongly read as PCM), video needs its label.
		f.essenceComplete = e.valid && (e.isAudio ? cls != "MPGA" : !e.essenceContainerLabel.isEmpty());
	}
} // namespace

// MARK: - Load

MdbDatabase MdbParser::load(const QString &mdbFilePath, bool *ok)
{
	if (ok)
		*ok = false;
	MdbDatabase db;

	QFile file(mdbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcMdb) << "cannot open" << mdbFilePath << file.errorString();
		return db;
	}
	const QByteArray data = file.readAll();
	file.close();

	BentoFile b;
	QString why;
	if (!b.load(data, &why))
	{
		qCWarning(lcMdb) << mdbFilePath << "is not a readable Avid MDB:" << why;
		return db;
	}
	if (ok)
		*ok = true;

	const Props p(b);
	if (p.mobId < 0)
	{
		qCDebug(lcMdb) << mdbFilePath << "carries no MobID property — an empty database";
		return db;
	}

	// Group every MOBJ object by its MobID. Avid writes the same MobID on
	// more than one object, so each clip is the union of its objects.
	QHash<QByteArray, quint32> objectByMob; ///< First object per MobID (for SCLP hops).
	QHash<QString, QVector<quint32>> objectsByHex;
	QVector<QString> order;
	for (quint32 obj : b.objectsWithProperty(p.mobId))
	{
		if (b.objectClass(obj) != "MOBJ")
			continue;
		const QByteArrayView raw = b.value(obj, p.mobId);
		if (raw.size() != MobId::kRawSize || raw == QByteArrayView(placeholderMob()))
			continue;
		const QByteArray rawBytes = raw.toByteArray();
		if (!objectByMob.contains(rawBytes))
			objectByMob.insert(rawBytes, obj);
		const QString hex = BentoFile::mobIdHex(raw);
		auto it = objectsByHex.find(hex);
		if (it == objectsByHex.end())
		{
			it = objectsByHex.insert(hex, {});
			order.append(hex);
		}
		it->append(obj);
	}

	int complete = 0;
	for (const QString &hex : order)
	{
		const QVector<quint32> &objs = objectsByHex[hex];

		// A file mob owns a media descriptor; a source mob owns an MDES (the
		// import/tape source — not needed); a master mob owns nothing.
		quint32 mediaObj = 0, mediaDesc = 0;
		bool anyPhysical = false;
		for (quint32 obj : objs)
		{
			const quint32 desc = BentoFile::handle(b.value(obj, p.physMedia));
			if (desc == 0)
				continue;
			anyPhysical = true;
			if (mediaObj == 0 && isMediaClass(b.objectClass(desc)))
			{
				mediaObj = obj;
				mediaDesc = desc;
			}
		}

		if (mediaObj != 0)
		{
			MdbFile f;
			f.mobIdHex = hex;
			for (quint32 obj : objs)
			{
				const QByteArrayView u = b.value(obj, p.usage);
				if (f.usageCode < 0 && !u.isEmpty())
					f.usageCode = int(BentoFile::uint(u));
			}
			readEssence(b, p, mediaObj, mediaDesc, objectByMob, f);
			if (f.essenceComplete)
				++complete;
			db.files.insert(hex, f);
			continue;
		}
		if (anyPhysical)
			continue; // a source mob

		MdbMaster m;
		m.mobIdHex = hex;
		QSet<quint32> seen;
		for (quint32 obj : objs)
		{
			if (m.clipName.isEmpty())
				m.clipName = BentoFile::string(b.value(obj, p.name));
			const QByteArrayView u = b.value(obj, p.usage);
			if (m.usageCode < 0 && !u.isEmpty())
				m.usageCode = int(BentoFile::uint(u));
			walkAttributes(b, p, BentoFile::handle(b.value(obj, p.attrs)), m, seen, 0);
		}
		if (!m.sourceFilePath.isEmpty())
			m.sourceFileName = baseName(m.sourceFilePath);
		db.masters.insert(hex, m);
	}

	qCDebug(lcMdb) << mdbFilePath << ":" << b.entryCount() << "TOC entries," << db.masters.size() << "clips,"
				   << db.files.size() << "files (" << complete << "complete )";
	return db;
}
