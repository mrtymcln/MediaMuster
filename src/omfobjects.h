#pragma once

// OMF-era (legacy Avid media, pre-MXF). An OMF essence file is an Apple
// Bento container with the essence first and the object table of contents
// at the tail; it lives flat in "OMFI MediaFiles" beside a version-2
// msmFMID.pmr (8-byte MOBs) and a msmMMOB.mdb whose mobs carry 12-byte
// omfi:UIDs instead of the 32-byte UMIDs every MXF-era source writes.
//
// This module is the OMF Interchange object walker — the code that, given
// an open BentoFile, follows a mob to its attributes (bin, source path,
// project, import flag), its media descriptor (codec, dims, rate, length,
// bits, channels) and its timecode component (drop frame, start, fps). It
// is SHARED with the MXF-era MDB reader: an MXF-era msmMMOB.mdb is the
// same OMF Interchange object store, so MdbParser delegates every walk
// here and owns only the file load, the MobID grouping and the
// master/file/source triage. Nothing in this file is MXF-specific; MXF
// header handling (MxfParser) and the MobID byte-order rules (MobId) live
// elsewhere and are unaffected.
//
// The OMF-era extensions live here rather than in a reader of their own
// because the OMF-era MDB needs every one of them too: 12-byte SourceID
// hops, WAVD/AIFD/SD2D audio descriptors whose facts sit in a RIFF/AIFF
// header blob, the 4CC + resolution-id codec path (OmfResolutions),
// WINL/UNXL locators beside MACL, and the _PJ / _MEDIAFILE attributes.
// Each is tagged "OMF-era:" at the line, so the MXF-era walk can be read
// past them. Every value is fetched through BentoFile::bytes(), which
// works in both the load (MDB) and the tail-first (essence file) modes.

#include "bentofile.h"
#include "avidprecompute.h"
#include "mxfparser.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QSet>
#include <QString>

namespace OmfObjects
{
	// OMF semantic version, independent of the Bento label's version.
	// OMF1 objects may also be stored in Bento2 (the toolkit's IMA mode).
	enum class Revision { Unknown, Omf1, Omf2 };
	[[nodiscard]] Revision revision(const BentoFile &b);
	[[nodiscard]] QByteArray normalizedMobId(const BentoFile &b, QByteArrayView raw);
	[[nodiscard]] bool isMobClass(const QByteArray &cls);
	// MARK: - Property ids, resolved once per file

	/// Every property the walker reads, resolved by name from the file's
	/// own dictionary. −1 when the file lacks a property: BentoFile reads a
	/// missing (object, property) as empty, so a −1 id simply yields
	/// nothing rather than a guard at every use.
	struct Props
	{
		Revision revision;
		bool omf2;
		int mobId, usage, name, editRate, attrs, physMedia;
		int attrRefs, attbName, attbKind, attbInt, attbString, attbObj;
		int binNameUtf8, binName, posixPath, pathUtf8, pathName;
		int essComp, resId, width, height, layout, sampleRate, length, compWidth, bits, channels;
		int tracks, trackComp, sequence, sourceId, tcFlags;

		// OMF-era: the 4CC codec family, the audio header blobs, the SD2
		// pair, the timecode start/rate, the media-data MobIDs an essence
		// file keys its data object by, the source-descriptor kind, the
		// UNIX locator's own path property, and the descriptor's locator
		// list (whose MSML names the last known volume).
		int compression, wavdSummary, aifdSummary, sd2dBits, sd2dChannels;
		int tcFps, tcStart;
		int mdatMobId, waveMobId, aifcMobId, sd2mMobId;
		int mobKind, unxlPath, locator, lastKnownVolumeUtf8, lastKnownVolume;
		int sd2dMobId, slotRate, nestedSlots, selected, choices, inputSegment;
		int winlPath, maclPath, tiffSummary, rgbaLayout, rgbaStructure;

		explicit Props(const BentoFile &b);
	};

	/// The first object carrying each raw MobID — the map SCLP source
	/// references are resolved through (a source reference names a mob by
	/// its bytes, not by object handle). OMF-era: keys are the raw bytes of
	/// whichever width the mob was written in (12 or 32), since a SourceID
	/// is written in the same width as the mob it names.
	using ObjectByMob = QHash<QByteArray, quint32>;

	// Direct source-mob references in a mob's segment graph; no ancestry hop.
	// Returning all targets lets callers reject ambiguous file/master matches.
	[[nodiscard]] QVector<quint32> sourceMobs(const BentoFile &b, const Props &p, quint32 mob,
										const ObjectByMob &objectByMob);
	[[nodiscard]] bool mobEditRate(const BentoFile &b, const Props &p, quint32 mob, qint32 &num, qint32 &den);

	// MARK: - Descriptor classes

	/// PCMA / MPGA / WAVE — the audio descriptor classes the MDB writes —
	/// plus, OMF-era, WAVD / AIFD / SD2D (WAVE, AIFF-C and Sound Designer II
	/// essence files, whose facts sit in a header blob rather than in
	/// MDAU properties).
	[[nodiscard]] bool isAudioClass(const QByteArray &cls);

	/// Audio plus CDCI / MPGI / RGBA / JPED: anything that describes essence
	/// (as opposed to MDES, the import/tape source descriptor).
	[[nodiscard]] bool isMediaClass(const QByteArray &cls);

	/// OMF-era: MACL / WINL / UNXL — the Macintosh, Windows and UNIX
	/// locator classes an attribute or a descriptor may point at. The MDB
	/// path only ever met MACL (measured: WINL 0, UNXL 0 across every
	/// MXF-era MDB on hand); OMF files written on Windows carry WINL.
	[[nodiscard]] bool isLocatorClass(const QByteArray &cls);

	// MARK: - Codec label

	/// OMFI:DIDD:EssenceCompression stores the SMPTE label as an AAF AUID:
	/// the first three GUID fields little-endian (u32, u16, u16) followed by
	/// the eight remaining bytes — and the eight remaining bytes are the
	/// label's FIRST half. So UL = bytes[8..16] + rev(bytes[0..4]) +
	/// rev(bytes[4..6]) + rev(bytes[6..8]). Measured byte-identical to MXF
	/// tag 0x3201 on every file that carries it. Empty unless 16 bytes.
	[[nodiscard]] QByteArray auidToUl(QByteArrayView auid);

	/// Legacy DNxHD essence carries no EssenceCompression in the MDB, only
	/// Avid's numeric resolution id. The DNxHD labels are numbered in step
	/// with it: label byte 13 == id − 1234 (verified on all ten ids in the
	/// corpus: 1235→01 … 1253→13). Ids outside the DNxHD range (2400, 3416,
	/// 557…) are other codecs that DO carry a label, so the guard matters —
	/// empty outside 1235..1489. Id 1244 is the one exception: MC 25.12
	/// registered it under the 0x0D version byte, so that spelling is
	/// returned for it — the 0x0A one names nothing in the app's table.
	[[nodiscard]] QByteArray ulFromResId(quint32 resId);

	// MARK: - OMF-era audio header blobs

	/// OMF-era: what OMFI:WAVD:Summary yields — the RIFF header of the WAVE
	/// file up to (not including) its data. Media Composer writes a `bext`
	/// chunk first, so the reader walks chunks to `fmt ` rather than
	/// trusting a fixed offset. `valid` is false when no `fmt ` chunk fits.
	struct WaveSummary
	{
		quint16 formatTag = 0; ///< 1 = PCM, 3 = IEEE float, 0xFFFE = extensible.
		int channels = 0;
		int sampleRate = 0;
		int bits = 0;
		bool valid = false;
	};

	/// OMF-era: what OMFI:AIFD:Summary yields — the FORM/AIFC header up to
	/// its `COMM` chunk (big-endian sizes, an 80-bit extended sample rate,
	/// and a compression type such as "in24" / "NONE" / "sowt").
	struct AifcSummary
	{
		int channels = 0;
		qint64 frames = 0;
		int bits = 0;
		int sampleRate = 0;
		QByteArray compressionType; ///< Empty for a plain AIFF `COMM`.
		bool valid = false;
	};

	/// OMF-era: chunk-walk a RIFF/WAVE header blob to its `fmt ` chunk.
	[[nodiscard]] WaveSummary readWaveSummary(QByteArrayView blob);

	/// OMF-era: chunk-walk a FORM/AIFF or FORM/AIFC header blob to `COMM`.
	[[nodiscard]] AifcSummary readAifcSummary(QByteArrayView blob);

	// MARK: - Attribute trees

	/// What a mob's attribute tree yields. Fields fill first-non-empty so
	/// the same struct can be walked from every object that shares a MobID
	/// (Avid writes the clip name on one object and the bin on another).
	struct Attributes
	{
		QString bin;			 ///< _ORG_BIN → MCBR → OMFI:MCBR:MC:binNameUTF8 (else MC:binName).
		QString sourceFilePath;	 ///< _IMPORTSETTING/_SRCFILE locator path, else _USER/`UNC Path`.
		QString sourceContainer; ///< _USER/Video or Audio — "QTFF" for a QuickTime import.
		QString project;		 ///< OMF-era: _PJ (kind 2) — the project the media was created in.
		QString mediaFilePath;	 ///< OMF-era: _MEDIAFILE locator path (diagnostic; often empty).
		bool isImported = false; ///< An _IMPORTSETTING attribute exists.
		/// OMF-era: set by the caller for a mob written with a 12-byte
		/// omfi:UID (or by the essence-file reader outright). Only then does
		/// _SRCFILE accept a WINL / UNXL target; an MXF-era (32-byte) mob
		/// keeps the MACL-only rule the MDB reader always had, so a
		/// Windows-written MXF-era database reports the same Source File it
		/// did before — the kind-2 `UNC Path` — whatever order its
		/// attributes come in.
		bool omfEra = false;
	};

	/// OMF-era: the path a locator object carries — OMFI:FL:POSIXPathName,
	/// else FL:PathNameUTF8, else FL:PathName, else OMFI:UNXL:PathName (the
	/// UNIX locator's own property). Works for MACL, WINL and UNXL alike.
	[[nodiscard]] QString locatorPath(const BentoFile &b, const Props &p, quint32 locator);

	/// ATTR → AttrRefs → ATTB[]; nested ATTRs are followed only where a
	/// fact we want lives below them (_IMPORTSETTING, _USER). `seen` guards
	/// the shared nodes Avid writes and must persist across every object
	/// walked into the same `a`. `attrObj` is the handle from
	/// OMFI:CPNT:Attributes; 0 is a no-op.
	void walkAttributes(const BentoFile &b, const Props &p, quint32 attrObj, Attributes &a, QSet<quint32> &seen,
						int depth);

	/// Media Composer 26.8's precompute display predicate, evaluated on each
	/// logical master separately. No recursive import search or combining
	/// one duplicate's import marker with another duplicate's video tracks.
	/// Unsupported schemas, incomplete evidence and disagreements stay unknown.
	[[nodiscard]] AvidPrecompute::Category precomputeCategory(const BentoFile &b, const Props &p,
															const QVector<quint32> &masters);

	// MARK: - Track walk (timecode, source mob)

	/// The first TCCP reachable from `mob`'s tracks: through SEQU
	/// components, and through SCLP source references into other mobs (the
	/// timecode lives on the tape/import source mob, not the file mob). 0
	/// if none. `seen` is shared with the recursion; pass a fresh set.
	[[nodiscard]] quint32 findTimecodeComponent(const BentoFile &b, const Props &p, quint32 mob,
												const ObjectByMob &objectByMob, QSet<quint32> &seen, int depth);

	/// The first mob a SCLP on `mob`'s tracks refers to, or 0 — the file
	/// mob's source mob (tape/import), which is where OMF-era files of one
	/// generation keep the _PJ project attribute. Follows SEQU; no hop.
	[[nodiscard]] quint32 findSourceMob(const BentoFile &b, const Props &p, quint32 mob,
										const ObjectByMob &objectByMob);

	/// What a TCCP carries. `dropFrame` is OMFI:TCCP:Flags != 0 (the MDB
	/// path's rule); OMF-era: `start` and `fps` are OMFI:TCCP:StartTC (in
	/// frames) and OMFI:TCCP:FPS, which an OMF essence file surfaces and an
	/// MXF-era MDB row does not need.
	struct Timecode
	{
		bool found = false;
		bool dropFrame = false;
		qint64 start = -1; ///< Frames; −1 when the property is absent.
		int fps = 0;
	};
	[[nodiscard]] Timecode readTimecode(const BentoFile &b, const Props &p, quint32 tccp);

	// MARK: - Descriptor

	/// Fill `e` from the media descriptor `desc` of the file mob `mobObj`
	/// the way a header parse would, then hand it to MxfParser::finalise so
	/// every derived value comes from the same code as the header path.
	/// Returns false — leaving `e` untouched — when `desc` is not a media
	/// class. `codecKnown` (optional) reports whether a codec was
	/// established (the stored AUID, one rebuilt from the resolution id,
	/// or — OMF-era, 12-byte file mobs only — a hit in the OmfResolutions
	/// table); the caller decides what "complete" means from that plus the
	/// descriptor class, since MPEG audio has no label here and would
	/// wrongly read as PCM.
	bool readDescriptor(const BentoFile &b, const Props &p, quint32 mobObj, quint32 desc,
						const ObjectByMob &objectByMob, MxfMetadata &e, bool *codecKnown = nullptr);
} // namespace OmfObjects
