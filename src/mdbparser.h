#pragma once

#include "mxfparser.h"
#include "omfobjects.h"

#include <QHash>
#include <QString>

// MARK: - Records

/// A master clip as `msmMMOB.mdb` describes it: the clip-level facts that
/// every V01/A01/A02 relative shares. Keyed by the master MOB — the same
/// id the PMR's MASTER record carries and the MXF's MaterialPackage UID
/// (after MobId::toPmrForm) resolves to.
struct MdbMasterMob
{
	QString mobIdHex;
	QString clipName;		 ///< OMFI:CPNT:Name — what Avid displays. Equal to the
							 ///< MXF MaterialPackage name on 360/360 + 795/795 files.
	QString bin;			 ///< _ORG_BIN → the bin's UTF-8 name. Exists nowhere else.
	QString sourceFilePath;	 ///< _IMPORTSETTING/_SRCFILE → the imported file's path.
	QString sourceFileName;	 ///< Basename of sourceFilePath.
	QString sourceContainer; ///< _USER/Video — "QTFF" for a QuickTime import.
	QString project;		 ///< OMF-era: _PJ on the master mob, for when the PMR has no project. Usually empty.
	bool isImported = false; ///< An _IMPORTSETTING attribute exists.
	bool classificationKnown = false; ///< Avid usage1/7 establishes precompute/media; absent OMF2 usage stays unknown.
	int usageCode = -1;		 ///< OMFI:MOBJ:UsageCode: 7 = master clip, 1 = precompute.
};

/// One essence file as the MDB describes it. Keyed by the file MOB — the
/// PMR's FILE record. `essence` is filled the way MxfParser fills it from
/// a header, then run through the same MxfParser::finalise, so a row built
/// from the database shows the same codec / resolution / fps / duration /
/// bit depth the header path would. `essenceComplete` is the scanner's
/// permission to skip the header read — false when the database cannot
/// name the codec (MPEG audio has no label in the MDB) or the descriptor
/// isn't a media descriptor.
struct MdbFileMob
{
	QString mobIdHex;
	QString masterMobId; ///< Unique master whose source-clip graph references this file; empty if ambiguous.
	int usageCode = -1; ///< 0 = media, 9 = precompute.
	MxfMetadata essence;
	bool essenceComplete = false;
	/// OMF-era: _PJ from the file mob, else from the source mob its SCLP
	/// points at — the two places OMF files keep the project (MC 2026 and
	/// the 2021 slates respectively). An OMF-era PMR has no project of its
	/// own, so this is where the scanner gets it. Empty on MXF-era rows.
	QString project;
};

/// The parsed contents of ONE msmMMOB.mdb — this is the database itself,
/// which is why it keeps the plain name. Its two members hold many records
/// each: MdbMasterMob for master clips, MdbFileMob for essence files, named
/// after Avid's own master mob / file mob (see the walk in mdbparser.cpp).
///
/// Split the way the scanner consumes it:
/// `files` is looked up once per row during the folder walk and dropped;
/// `masters` is kept for the header pass's UMID re-join. Both are keyed by
/// MediaMuster's dotted MOB hex (MobId::format). OMF-era: a 12-byte
/// omfi:UID is keyed by its wrapped 32-byte form (OmfUid::canonicalHex),
/// the same string the v2 PMR yields, so the join needs no second form.
struct MdbDatabase
{
	OmfObjects::Revision revision = OmfObjects::Revision::Unknown;
	QHash<QString, MdbMasterMob> masters;
	QHash<QString, MdbFileMob> files;
	[[nodiscard]] bool isEmpty() const { return masters.isEmpty() && files.isEmpty(); }
};

// MARK: - MdbParser

/// Reads `msmMMOB.mdb`, the per-folder Avid clip database — an OMF
/// Interchange object store in a Bento container (see BentoFile). Walks the
/// table of contents, not the bytes: every value is reached by its property
/// name, resolved from the dictionary the file itself carries.
///
/// What it reads and how it was verified (360 live files + 795 archived
/// headers, MXF as ground truth — see the decode notes in mdbparser.cpp):
/// clip name, bin, source path/container, import flag, usage codes; and per
/// file mob the descriptor class (audio/video), codec label (two spellings),
/// stored dims + layout, sample rate, length, bits, channels, drop frame.
///
/// Two facts about the database the caller must respect: it holds NO
/// filenames (the PMR is the filename→MOB index; this is MOB→facts), and
/// it is stale-inclusive (records for media deleted long ago stay in it),
/// so nothing here says what exists on disk.
class MdbParser
{
public:
	/// Load and index the database. `ok` (optional) is false when the file
	/// can't be opened or isn't a Bento container whose label and table of
	/// contents agree — a stronger gate than the old marker scan. ok=true with
	/// empty maps is a valid, empty database. Never throws.
	[[nodiscard]] static MdbDatabase load(const QString &mdbFilePath, bool *ok = nullptr);
};
