#include "mdbparser.h"
#include "bentofile.h"
#include "logcategories.h"
#include "omfobjects.h"
#include "omfuid.h" // OMF-era: widens the 12-byte omfi:UID to the 32-byte key form.

#include <QByteArrayView>
#include <QFile>
#include <QSet>
#include <QVector>

// Reads msmMMOB.mdb through its Bento table of contents.
//
// MARK: - What the database is
//
// `msmMMOB.mdb` is an OMF Interchange object store (Avid's pre-AAF
// format; the spec is public) inside an Apple Bento container (BentoFile).
// The captured Avid databases use legacy MOBJ objects with a 32-byte
// OMFI:MOBJ:MobID — the same UMID the PMR and the MXF carry, in Avid's byte
// order (PMR↔MDB join raw; MXF needs MobId::toPmrForm). Standard OMF2 instead
// uses explicit MMOB/SMOB/CMOB classes, mob slots and MediaDescription. Both
// schemas are handled independently of Bento container revision. Properties are named
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
// The object walks themselves (attribute tree, descriptor, timecode hop)
// live in OmfObjects (omfobjects.{h,cpp}) since 2026-09-02: an OMF-era
// essence file is the same object store, so one walker serves both. This
// file keeps what is database-specific — the load, the MobID grouping and
// the master/file/source triage — and the decode notes above, which is
// where every rule the walker applies was verified.
//
// OMF-era: the msmMMOB.mdb inside an "OMFI MediaFiles" folder is the same
// store with three differences, all handled below or in OmfObjects:
// OMFI:MOBJ:MobID is a 12-byte omfi:UID (MC 2026 additionally writes a
// 32-byte UMID on the physical mob of the same file, so both widths are
// accepted and keyed by OmfUid::canonicalHex); the audio descriptors are
// WAVD/AIFD/SD2D; the codec is a 4CC + resolution id rather than a label;
// and the project name is a _PJ attribute on the file or source mob, since
// a version-2 PMR carries no project. A 32-byte physical mob owns an MDES
// and therefore falls out of the triage as a source mob, as before.
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
	/// Avid's placeholder MOB — byte-identical across unrelated projects, in no
	/// MXF. Never a real clip; skipped on sight.
	const QByteArray &placeholderMob()
	{
		static const QByteArray kMob =
			QByteArray::fromHex("060a2b340101010001010f0013000000bacc8a260647d528e5cd3f5b5f40443f");
		return kMob;
	}

	QString baseName(const QString &path)
	{
		const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
		return slash < 0 ? path : path.mid(slash + 1);
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

	const OmfObjects::Props p(b);
	db.revision = p.revision;
	if (p.mobId < 0)
	{
		qCDebug(lcMdb) << mdbFilePath << "carries no MobID property — an empty database";
		return db;
	}

	// Group every MOBJ object by its MobID. Avid writes the same MobID on
	// more than one object, so each clip is the union of its objects.
	OmfObjects::ObjectByMob objectByMob; ///< First object per MobID (for SCLP hops).
	QHash<QString, QVector<quint32>> objectsByHex;
	QVector<QString> order;
	for (quint32 obj : b.objectsWithProperty(p.mobId))
	{
		if (!OmfObjects::isMobClass(b.objectClass(obj)))
			continue;
		const QByteArray raw = OmfObjects::normalizedMobId(b, b.bytes(obj, p.mobId));
		// OMF-era: canonicalHex accepts a 12-byte omfi:UID (wrapped to the
		// 32-byte form the v2 PMR yields) beside the 32-byte UMID, and reads
		// empty for any other width; a 32-byte id formats exactly as before.
		const QString hex = OmfUid::canonicalHex(raw);
		if (hex.isEmpty() || raw == QByteArrayView(placeholderMob()))
			continue;
		const QByteArray rawBytes = raw;
		if (!objectByMob.contains(rawBytes))
			objectByMob.insert(rawBytes, obj);
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
		bool explicitMaster = false;
		bool legacyMaster = false;
		for (quint32 obj : objs)
		{
			explicitMaster |= b.objectClass(obj) == "MMOB";
			const auto usage = b.read(obj, p.usage);
			legacyMaster |= usage.ok() && (b.uintValue(usage.data) == 7 || b.uintValue(usage.data) == 1);
			anyPhysical |= b.hasProperty(obj, p.physMedia);
			const quint32 desc = b.ref(obj, p.physMedia);
			if (desc == 0)
				continue;
			anyPhysical = true;
			if (mediaObj == 0 && OmfObjects::isMediaClass(b.objectClass(desc)))
			{
				mediaObj = obj;
				mediaDesc = desc;
			}
		}

		if (mediaObj != 0)
		{
			MdbFileMob f;
			f.mobIdHex = hex;
			f.essence.fileMobId = hex;
			for (quint32 obj : objs)
			{
				const QByteArrayView u = b.value(obj, p.usage);
				if (f.usageCode < 0 && !u.isEmpty())
					f.usageCode = int(b.uintValue(u));
			}
			// Skipping the header requires usable technical facts, not just a
			// recognised descriptor class with missing/unreadable properties.
			bool codecKnown = false;
			if (OmfObjects::readDescriptor(b, p, mediaObj, mediaDesc, objectByMob, f.essence, &codecKnown))
			{
				const auto length = b.read(mediaDesc, p.length);
				f.essenceComplete =
					f.essence.valid && codecKnown && length.ok() && (length.data.size() == 4 || length.data.size() == 8) &&
					b.int64Value(length.data) >= 0 &&
					(f.essence.isAudio ? f.essence.sampleRate > 0 && f.essence.channels > 0 && !f.essence.bitDepth.isEmpty()
									   : !f.essence.fps.isEmpty());
			}
			if (f.essenceComplete)
				++complete;

			// OMF-era: the project lives on the file mob (MC 2026) or on the
			// source mob its SCLP names (the 2021 slates); the file mob wins.
			// An MXF-era file mob carries neither, so this reads empty there.
			{
				OmfObjects::Attributes a;
				a.omfEra = b.value(mediaObj, p.mobId).size() == OmfUid::kUidSize; // OMF-era: 12-byte mob
				QSet<quint32> seen;
				for (quint32 obj : objs)
					OmfObjects::walkAttributes(b, p, b.ref(obj, p.attrs), a, seen, 0);
				if (a.project.isEmpty())
				{
					const quint32 src = OmfObjects::findSourceMob(b, p, mediaObj, objectByMob);
					OmfObjects::walkAttributes(b, p, b.ref(src, p.attrs), a, seen, 0);
				}
				f.project = a.project;
			}

			// OMF-era: the descriptor's locator list may name the volume the
			// file was last seen on (MSML). Diagnostic only — never a fact.
			if (lcMdb().isDebugEnabled())
			{
				for (quint32 loc : b.refs(mediaDesc, p.locator))
				{
					if (b.objectClass(loc) != "MSML")
						continue;
					QString volume = BentoFile::utf8String(b.value(loc, p.lastKnownVolumeUtf8));
					if (volume.isEmpty())
						volume = BentoFile::string(b.value(loc, p.lastKnownVolume));
					if (!volume.isEmpty())
						qCDebug(lcMdb) << hex << "last known volume" << volume;
				}
			}
			db.files.insert(hex, f);
			continue;
		}
		if (anyPhysical || (p.omf2 && !explicitMaster) ||
			(p.revision == OmfObjects::Revision::Omf1 && !legacyMaster))
			continue; // a source mob

		MdbMasterMob m;
		m.mobIdHex = hex;
		OmfObjects::Attributes a;
		// OMF-era: a 12-byte mob may point _SRCFILE at a WINL/UNXL; a 32-byte
		// (MXF-era) mob keeps the MACL-only rule, so its Source File is
		// exactly what it was before the walker learned the other locators.
		a.omfEra = b.value(objs.first(), p.mobId).size() == OmfUid::kUidSize;
		QSet<quint32> seen;
		for (quint32 obj : objs)
		{
			if (m.clipName.isEmpty())
				m.clipName = BentoFile::string(b.value(obj, p.name));
			const QByteArrayView u = b.value(obj, p.usage);
			if (m.usageCode < 0 && !u.isEmpty())
				m.usageCode = int(b.uintValue(u));
			OmfObjects::walkAttributes(b, p, b.ref(obj, p.attrs), a, seen, 0);
		}
		m.bin = a.bin;
		m.classificationKnown = m.usageCode == 1 || m.usageCode == 7;
		m.sourceFilePath = a.sourceFilePath;
		m.sourceContainer = a.sourceContainer;
		m.project = a.project; // OMF-era: _PJ on a master mob, when a file keeps it there.
		m.isImported = a.isImported;
		if (!m.sourceFilePath.isEmpty())
			m.sourceFileName = baseName(m.sourceFilePath);
		db.masters.insert(hex, m);
	}

	// Reverse the master -> source-clip -> file link. A file referenced by
	// two distinct masters has no unique answer; never choose hash order.
	QHash<QString, QSet<QString>> mastersByFile;
	for (auto master = db.masters.cbegin(); master != db.masters.cend(); ++master)
		for (quint32 obj : objectsByHex.value(master.key()))
			for (quint32 target : OmfObjects::sourceMobs(b, p, obj, objectByMob))
			{
				const QString fileHex = OmfUid::canonicalHex(OmfObjects::normalizedMobId(b, b.bytes(target, p.mobId)));
				if (db.files.contains(fileHex))
					mastersByFile[fileHex].insert(master.key());
			}
	for (auto file = db.files.begin(); file != db.files.end(); ++file)
	{
		const auto masters = mastersByFile.value(file.key());
		if (masters.size() == 1)
			file->masterMobId = *masters.cbegin();
	}

	qCDebug(lcMdb) << mdbFilePath << ":" << b.entryCount() << "TOC entries," << db.masters.size() << "clips,"
				   << db.files.size() << "files (" << complete << "complete )";
	return db;
}
