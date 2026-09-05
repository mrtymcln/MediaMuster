// OMF-era (legacy Avid media, pre-MXF). The reader for one OMF essence
// file — see omfparser.h for where it sits. Nothing here is MXF-specific;
// the walks are OmfObjects', shared with the MDB reader, and only the
// triage and the field mapping onto MxfMetadata live in this file.
//
// MARK: - What the file holds (read from the fixtures)
//
// A HEAD object (object 1) indexes the mobs — OMFI:SourceMobs,
// OMFI:CompositionMobs, OMFI:MediaData — but the reader does not need it:
// the file's MOBJ objects are found through the property dictionary just
// as in an MDB. Three mobs per file, plus one media-data object:
//
//   media-data   (class JPEG / WAVE / AIFC / SD2M; owns the essence value)
//     OMFI:MDAT:MobID | WAVE:MobID | AIFC:MobID | SD2M:MobID  — the FILE
//     mob's 12-byte omfi:UID, which is how the file mob is told apart from
//     its relatives without trusting usage codes.
//   master mob   (no PhysicalMedia; UsageCode 7, or 1 for a precompute)
//     OMFI:CPNT:Name = the clip name; _ORG_BIN → bin; _IMPORTSETTING /
//     _SRCFILE → the imported file's path (WINL on the 2021 slates).
//   file mob     (PhysicalMedia → a media descriptor: JPED/CDCI/MPGI video,
//                 WAVD/AIFD/SD2D audio) — OmfObjects::readDescriptor;
//     _MEDIAFILE → the file's own locator; _PJ here in MC 2026's files.
//   source mob   (PhysicalMedia → MDES; 12-byte in the 2021 slates, a
//                 32-byte UMID "physical mob" in MC 2026's) — reached by
//     the file mob's SCLP. Owns the TCCP (start, fps, drop) and, in the
//     2021 slates, the _PJ project attribute; on the oldest 15 slates its
//     MDES locator (a WINL) is the only place the import path is written.
//
// Verified on all 80 shipped slates and both MC 2026 audio files: every
// one yields a named codec, a master clip name, a project, and a file mob
// id equal to what its version-2 PMR stores (tst_omfparser).

#include "omfparser.h"
#include "avidusage.h"
#include "bentofile.h"
#include "logcategories.h"
#include "omfobjects.h"
#include "omfuid.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QVector>

namespace
{
	/// The objects that share one MobID, in file order. Avid writes the
	/// same MobID on more than one MOBJ, so every fact is first-non-empty
	/// across the group — the MDB reader's rule.
	struct MobGroup
	{
		QString hex;
		QVector<quint32> objects;
		quint32 mediaObj = 0;  ///< The object whose PhysicalMedia is a media descriptor.
		quint32 mediaDesc = 0; ///< That descriptor.
		bool anyPhysical = false;
		int usageCode = -1;
		bool masterClass = false;
		bool legacyMaster = false;
	};

	/// Embedded media identity. More than one distinct ID cannot be
	/// represented by this single-essence API without choosing arbitrarily.
	QByteArray mediaDataMobId(const BentoFile &b, const OmfObjects::Props &p, bool &ambiguous)
	{
		QSet<QByteArray> ids;
		for (int prop : {p.mdatMobId, p.waveMobId, p.aifcMobId, p.sd2mMobId, p.sd2dMobId})
		{
			if (prop < 0)
				continue;
			for (quint32 obj : b.objectsWithProperty(prop))
			{
				const QByteArray raw = OmfObjects::normalizedMobId(b, b.bytes(obj, prop));
				if (!OmfUid::canonicalHex(raw).isEmpty())
					ids.insert(raw);
			}
		}
		ambiguous = ids.size() > 1;
		return ids.size() == 1 ? *ids.cbegin() : QByteArray();
	}
} // namespace

// MARK: - Parse

OmfMetadata OmfParser::parseHeader(const QString &filePath, qint64 *bytesRead)
{
	OmfMetadata out;
	BentoFile b;
	QString why;
	const bool opened = b.open(filePath, &why);
	if (bytesRead)
		*bytesRead = b.bytesRead();
	if (!opened)
	{
		// No supported Bento tail or embedded omfi chunk was found.
		qCDebug(lcOmf) << filePath << "is not an OMF container:" << why;
		return out;
	}

	const OmfObjects::Props p(b);
	out.revision = p.revision;
	out.essence.headerStatus = MxfMetadata::HeaderStatus::Incomplete;
	if (p.mobId < 0)
	{
		qCDebug(lcOmf) << filePath << "carries no MobID property — a Bento container without mobs";
		return out;
	}

	bool ambiguousData = false;
	const QByteArray fileMobRaw = mediaDataMobId(b, p, ambiguousData);
	if (ambiguousData)
		return out;
	out.fileMobId = OmfUid::canonicalHex(fileMobRaw);

	// Group every MOBJ by its canonical hex and note what each owns, the
	// way MdbParser::load does — 12-byte omfi:UIDs and the 32-byte UMID MC
	// 2026 puts on the physical mob both key cleanly through OmfUid.
	OmfObjects::ObjectByMob objectByMob;
	QHash<QString, MobGroup> groups;
	QVector<QString> order;
	for (quint32 obj : b.objectsWithProperty(p.mobId))
	{
		if (!OmfObjects::isMobClass(b.objectClass(obj)))
			continue;
		const QByteArray raw = OmfObjects::normalizedMobId(b, b.bytes(obj, p.mobId));
		const QString hex = OmfUid::canonicalHex(raw);
		if (hex.isEmpty())
			continue;
		if (!objectByMob.contains(raw))
			objectByMob.insert(raw, obj);
		auto it = groups.find(hex);
		if (it == groups.end())
		{
			it = groups.insert(hex, MobGroup{hex, {}, 0, 0, false, -1});
			order.append(hex);
		}
		it->objects.append(obj);
		it->masterClass |= b.objectClass(obj) == "MMOB";
		const auto usage = b.read(obj, p.usage);
		if (usage.status != BentoFile::ReadStatus::Missing)
		{
			const qint32 code = usage.ok() && usage.data.size() == 4 ? AvidUsage::integerCode(b.uintValue(usage.data)) : AvidUsage::kInvalidOrConflicting;
			it->legacyMaster |= AvidUsage::isMasterCode(code);
			it->usageCode = AvidUsage::merge(it->usageCode, code);
		}
		// A present but unreadable descriptor is not evidence of a master.
		it->anyPhysical |= b.hasProperty(obj, p.physMedia);
		const quint32 desc = b.ref(obj, p.physMedia);
		if (desc == 0)
			continue;
		it->anyPhysical = true;
		if (it->mediaObj == 0 && OmfObjects::isMediaClass(b.objectClass(desc)))
		{
			it->mediaObj = obj;
			it->mediaDesc = desc;
		}
	}

	// Select the embedded data's file mob, or the unique media descriptor
	// when the file omits its data ID. Master identity is resolved through
	// source-clip links below (OMF1 usage 7/1, or OMF2 MMOB class).
	const MobGroup *fileMob = nullptr;
	const MobGroup *master = nullptr;
	for (const QString &hex : order)
	{
		const MobGroup &g = groups[hex];
		if (g.mediaObj == 0 || (!out.fileMobId.isEmpty() && hex != out.fileMobId))
			continue;
		if (fileMob)
			return out; // several media files; a single-row API cannot select one safely
		fileMob = &g;
	}
	if (!fileMob)
	{
		qCWarning(lcOmf) << "no media descriptor in" << filePath << "(read" << b.bytesRead() << "bytes)";
		if (bytesRead)
			*bytesRead = b.bytesRead();
		return out;
	}
	if (out.fileMobId.isEmpty() || out.fileMobId != fileMob->hex)
		out.fileMobId = fileMob->hex;

	// A composition is not a master. Match the actual source-clip graph
	// to the selected file mob, and leave identity unknown on ambiguity.
	for (const QString &hex : order)
	{
		const MobGroup &g = groups[hex];
		if (p.omf2 ? !g.masterClass : (g.anyPhysical || (p.revision == OmfObjects::Revision::Omf1 && !g.legacyMaster)))
			continue;
		bool referencesFile = false;
		for (quint32 obj : g.objects)
			for (quint32 target : OmfObjects::sourceMobs(b, p, obj, objectByMob))
				referencesFile |= fileMob->objects.contains(target);
		if (!referencesFile)
			continue;
		if (master)
		{
			master = nullptr;
			break;
		}
		master = &g;
	}

	MxfMetadata &e = out.essence;
	OmfObjects::readDescriptor(b, p, fileMob->mediaObj, fileMob->mediaDesc, objectByMob, e);
	e.fileMobId = out.fileMobId;

	// Identity from the master: the clip name Avid displays and the id the
	// PMR's MASTER record and the bins carry. A file with no master mob
	// keeps a valid essence and an empty identity, as an MXF header with
	// no MaterialPackage does.
	if (master)
	{
		e.hasMaterialPackage = true;
		e.umid = master->hex;
		for (quint32 obj : master->objects)
		{
			if (e.clipName.isEmpty())
				e.clipName = BentoFile::string(b.bytes(obj, p.name));
		}
		e.clipNameFromMaterial = !e.clipName.isEmpty();
		const auto classification = AvidUsage::masterClassification(master->usageCode);
		e.isPrecompute = classification == AvidUsage::Classification::Precompute;
		e.classificationKnown = classification != AvidUsage::Classification::Unknown;
		if (e.isPrecompute)
			e.precomputeCategory = OmfObjects::precomputeCategory(b, p, master->objects);
	}

	// Attributes, first-non-empty in the order the facts are trusted:
	// master (bin, import path, container), then the file mob
	// (_MEDIAFILE, MC 2026's _PJ), then the source mob (the 2021 slates'
	// _PJ). `seen` persists across all three because Avid shares ATTR
	// nodes between mobs; a shared node's facts were already taken.
	OmfObjects::Attributes a;
	a.omfEra = true; // an essence file is OMF-era by definition: _SRCFILE may be a WINL/UNXL
	QSet<quint32> seen;
	if (master)
		for (quint32 obj : master->objects)
			OmfObjects::walkAttributes(b, p, b.ref(obj, p.attrs), a, seen, 0);
	e.hasImportSetting = a.isImported; // the master's flag only, as the MDB reads it
	for (quint32 obj : fileMob->objects)
		OmfObjects::walkAttributes(b, p, b.ref(obj, p.attrs), a, seen, 0);

	// The source mob and every object sharing its id, resolved once for the
	// two facts below that may live there.
	const quint32 src = OmfObjects::findSourceMob(b, p, fileMob->mediaObj, objectByMob);
	QVector<quint32> srcObjs;
	if (src != 0)
	{
		const QString srcHex = OmfUid::canonicalHex(OmfObjects::normalizedMobId(b, b.bytes(src, p.mobId)));
		const auto it = groups.constFind(srcHex);
		srcObjs = it == groups.cend() ? QVector<quint32>{src} : it->objects;
	}
	if (a.project.isEmpty())
		for (quint32 obj : srcObjs)
			OmfObjects::walkAttributes(b, p, b.ref(obj, p.attrs), a, seen, 0);

	// The oldest slates (15 of the 80 shipped: the JFIF12S/14S/35/42 and
	// DV411 families, 2001-era) carry no _SRCFILE at all; their import path
	// is the WINL locator on the source mob's MDES. It is a locator, not an
	// import setting, so hasImportSetting stays whatever the master said.
	// MSML is skipped: MC 2026 uses it for the last-known volume, not a path.
	if (a.sourceFilePath.isEmpty())
	{
		for (quint32 obj : srcObjs)
		{
			const quint32 desc = b.ref(obj, p.physMedia);
			if (desc == 0 || (!p.omf2 && b.objectClass(desc) != "MDES"))
				continue;
			for (quint32 loc : b.refs(desc, p.locator))
			{
				if (b.objectClass(loc) == "MSML")
					continue;
				a.sourceFilePath = OmfObjects::locatorPath(b, p, loc);
				if (!a.sourceFilePath.isEmpty())
					break;
			}
			if (!a.sourceFilePath.isEmpty())
				break;
		}
	}
	out.bin = a.bin;
	out.mediaFilePath = a.mediaFilePath;
	e.projectName = a.project;
	e.sourceFilePath = a.sourceFilePath;
	e.sourceContainer = a.sourceContainer;

	// Timecode: the TCCP sits on the source mob, reached through the file
	// mob's SCLP (readDescriptor already took drop frame from the same
	// component). The master's tracks are the fallback route.
	{
		QSet<quint32> tcSeen;
		quint32 tccp = OmfObjects::findTimecodeComponent(b, p, fileMob->mediaObj, objectByMob, tcSeen, 0);
		if (tccp == 0 && master)
			for (quint32 obj : master->objects)
				if ((tccp = OmfObjects::findTimecodeComponent(b, p, obj, objectByMob, tcSeen, 0)) != 0)
					break;
		const OmfObjects::Timecode tc = OmfObjects::readTimecode(b, p, tccp);
		if (tc.found)
		{
			out.startTimecode = tc.start;
			out.timecodeFps = tc.fps;
			e.dropFrame = tc.dropFrame;
		}
	}

	if (bytesRead)
		*bytesRead = b.bytesRead();
	e.headerStatus = e.valid && !out.fileMobId.isEmpty() && master
						 ? MxfMetadata::HeaderStatus::Complete
						 : MxfMetadata::HeaderStatus::Incomplete;
	if (!e.valid)
		qCWarning(lcOmf) << "no usable OMF metadata in" << filePath << "(read" << b.bytesRead() << "bytes)";
	else
		qCDebug(lcOmf) << filePath << ":" << e.codec << e.resolution << e.fps << "clip" << e.clipName << "project"
					   << e.projectName << "bin" << out.bin << "(read" << b.bytesRead() << "bytes)";
	return out;
}
