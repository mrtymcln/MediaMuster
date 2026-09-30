#pragma once

#include "mediametadata.h"
#include "omfobjects.h"

#include <QHash>
#include <QString>

// MARK: - Records

/// A master clip as `msmMMOB.mdb` describes it: the clip-level facts that
/// every V01/A01/A02 relative shares. Keyed by the master MOB — the same
/// id the PMR's MASTER record carries and the MXF's MaterialPackage UID
/// (after MobId::swapMaterialByteOrder) resolves to.
struct MdbMasterMob
{
	QString mobIdHex;
	QString clipName;				  ///< OMFI:CPNT:Name — the clip name recorded by Avid.
	QString bin;					  ///< _ORG_BIN → original bin name; AVB and OMF readers can also supply it.
	QString sourceFilePath;			  ///< _IMPORTSETTING/_SRCFILE → the imported file's path.
	QString sourceFileName;			  ///< Basename of sourceFilePath.
	QString sourceContainer;		  ///< _USER/Video — "QTFF" for a QuickTime import.
	QString project;				  ///< _PJ on the master mob; fallback when the PMR project is empty.
	bool isImported = false;		  ///< An _IMPORTSETTING attribute exists.
	bool classificationKnown = false; ///< Avid usage1/7 establishes precompute/media; absent OMF2 usage stays unknown.
	int usageCode = -1;				  ///< OMFI:MOBJ:UsageCode: 7 = master clip, 1 = precompute.
	AvidPrecompute::Category precomputeCategory = AvidPrecompute::Category::Unknown;
};

/// One essence file, keyed by the file MOB from the PMR's FILE record.
/// Metadata uses the same finalisation as media headers. `essenceComplete`
/// tells the scanner whether the descriptor supplies the required technical
/// fields, helping it decide whether a header read is needed.
struct MdbFileMob
{
	QString mobIdHex;
	QString masterMobId; ///< Unique master whose source-clip graph references this file; empty if ambiguous.
	int usageCode = -1;	 ///< 0 = NoSpecialUsage, 9 = PrecomputeFile; classification comes from the master.
	MediaMetadata essence;
	bool essenceComplete = false;
	/// _PJ from the file mob, else its unique linked source mob.
	/// Used when the PMR project is empty.
	QString project;
};

/// Master and file records from one msmMMOB.mdb. The scanner consumes `files`
/// during the folder walk and retains `masters` for header lookups. Keys use
/// OmfUid::toIdText: 32-byte IDs retain their dotted MOB form; Avid's
/// prefix-42 OMF IDs use the same wrapper as legacy PMRs; other 12-byte
/// IDs preserve all three words in an `omf:` namespace.
struct MdbDatabase
{
	OmfObjects::Revision revision = OmfObjects::Revision::Unknown;
	QHash<QString, MdbMasterMob> masters;
	QHash<QString, MdbFileMob> files;
	[[nodiscard]] bool isEmpty() const { return masters.isEmpty() && files.isEmpty(); }
};

// MARK: - MdbParser

/// Reads msmMMOB.mdb, an OMF object store in a Bento container. Properties
/// are resolved through the file's dictionary and table of contents.
/// Supplies clip details and per-file audio/video metadata, but no media
/// filenames. Records may outlive their files; check the filesystem separately.
class MdbParser
{
public:
	/// Load and index the database. `ok` (optional) is false when the file
	/// can't be opened or isn't a Bento container whose label and table of
	/// contents agree. ok=true with empty maps is a valid, empty database.
	/// Never throws.
	[[nodiscard]] static MdbDatabase load(const QString &mdbFilePath, bool *ok = nullptr);
};
