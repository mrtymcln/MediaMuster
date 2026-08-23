#pragma once

#include <QByteArray>
#include <QString>

// MARK: - Shared codec names

/// Display name for PCM audio essence, shared by the codec table, the
/// no-descriptor fallback, and the demo data so a rebrand can't fork
/// the spelling. Classification never compares against it — the parser
/// decides audio-ness from the UL bytes themselves (see
/// isAudioEssenceLabel in mxfparser.cpp). The tests keep raw
/// "PCM Audio" literals on purpose, as the rename tripwire.
inline constexpr char kPcmAudioName[] = "PCM"; // was: PCM Audio

// MARK: - MxfMetadata

/// Technical metadata pulled from one MXF file's header partition.
/// `valid=false` means the parser didn't find enough information to
/// classify the file, usually a corrupt or non-Avid MXF. Empty
/// strings mean 'field absent in this MXF', not 'field is empty'.
struct MxfMetadata
{
	QString codec;		///< Resolved codec name, e.g. 'Avid DNx HQ (DNxHD 220)'.
	QString resolution; ///< '1920x1080', or empty for audio.
	QString fps;		///< '23.976', '25', '29.97', etc.
	QString bitDepth;	///< '8-bit', '10-bit', '24-bit'.
	QString umid;		///< Canonical hex UMID from tag 0x4401 (MaterialPackage, or SourcePackage fallback).
	QString clipName;	///< Clip name from tag 0x4402 in the material package.

	/// True when clipName came from the MaterialPackage (the master clip —
	/// what Avid/MediaInfo display) rather than a SourcePackage fallback
	/// (tape/file source). Only a material name is a rung of the clip-name
	/// ladder (see MediaFile::ClipNameSource); a source name is not.
	bool clipNameFromMaterial = false;

	/// True when the MaterialPackage's UsageCode (local tag 0x4408) is
	/// `Usage_LowerLevel` — Avid's mark for a rendered effect. Ordinary media
	/// carries no UsageCode at all, and neither does a mixdown (a mixdown is a
	/// master clip, not a precompute). This is the whole Media-vs-Precompute
	/// test; see the note above isPrecomputeUsage in mxfparser.cpp.
	bool isPrecompute = false;

	/// Avid's own TaggedValues (set 0x3F, on the packages' attribute lists):
	/// `UNC Path` = the imported file's path, `Video` = its container ("QTFF"),
	/// whether an `_IMPORTSETTING` attribute exists at all (absent on
	/// Avid-generated media — renders, tones, mixdowns), and `_PJ` (legacy
	/// `PROJNAME`) = the project the media was created in. The MDB carries the
	/// same facts; this is how the header path — the only path an Interplay
	/// site has — gets them too. The project is exactly what Media Composer
	/// itself reads from the file when it rebuilds a folder's PMR.
	QString sourceFilePath;
	QString sourceContainer;
	bool hasImportSetting = false;
	QString projectName;

	QByteArray essenceContainerLabel; ///< Raw essence-coding UL (usually 16 bytes), looked up against kEntries.
	int width = 0;
	int height = 0;		///< Stored value; interlaced files store one field height.
	int channels = 0;	///< Audio only.
	int sampleRate = 0; ///< Audio Hz.

	/// 0 = Full Frame, 1 = Separate Fields, 2 = Single Field, 3 = Mixed Fields.
	/// Used to decide whether to double `height` and to pick `i` vs `p`
	/// for DV codec name formatting.
	int frameLayout = -1;

	/// Set by a producer whose `height` is already the full frame. The MXF
	/// header stores one FIELD height for layout 1 (finalise doubles it);
	/// the MDB stores half heights for layouts 1 AND 3 and normalises them
	/// itself, then sets this so finalise doesn't double a second time while
	/// `frameLayout` still carries the real value for the DV i/p suffix.
	bool heightIsFrameHeight = false;

	/// Duration in frames at the clip's edit rate, for video AND audio —
	/// the Avid-bin timecode model (see the duration note in
	/// parseFromBuffer). 0 = unknown.
	qint64 durationFrames = 0;

	/// Nominal timecode base (24, 25, 30...). Video: from the 0x3001 edit
	/// rate. Audio: derived from the frame-track duration against the WAVE
	/// sample count in parseFromBuffer. 0 = underivable.
	int timecodeBase = 0;

	/// A timecode component (set 0x14) carried DropFrame=true (tag 0x1503).
	bool dropFrame = false;

	/// Duration from the essence descriptor's ContainerDuration (tag 0x3002),
	/// in the descriptor's edit units — frames for video, samples for audio.
	/// Collected separately from the structural-component durations because
	/// an audio header mixes units; parseFromBuffer resolves the two into
	/// `durationFrames` (see the unit note there).
	qint64 descriptorDuration = 0;

	bool isAudio = false;
	bool valid = false;
};

// MARK: - MxfParser

/// Reads an MXF file's header partition and pulls out the metadata
/// the table needs (codec, resolution, fps, duration, UMID, clip
/// name). Only the first 256–512 KB is read; essence data is never
/// touched. See `mxfparser.cpp` for the KLV walk and the codec lookup.
class MxfParser
{
public:
	/// Parse the MXF file's header. On success, `valid=true` and the
	/// metadata fields are populated. A file it can't open returns a
	/// default-constructed MxfMetadata; a file it opens but can't classify
	/// (no recognisable descriptors) returns whatever fields it did read,
	/// with `valid=false`. A missing header-partition pack is not fatal —
	/// the KLV walk simply starts from offset 0.
	///
	/// `bytesRead`, if non-null, receives the number of bytes the
	/// parser actually read from disk; useful for telemetry in the
	/// scanner's "MXF parse" summary log line.
	[[nodiscard]] static MxfMetadata parseHeader(const QString &filePath,
												 qint64 *bytesRead = nullptr);

	/// Map a 16-byte essence-container UL to the marketing name like
	/// 'Avid DNx HQ (DNxHD 220)'. `fps` is needed to resolve the
	/// DNxHD bitrate (the UL identifies the tier but not the rate).
	/// A UL not in the table returns the family inferred from its byte
	/// structure ('VC-3 (unknown variant: HEX)'), or 'Unknown (HEX)' when
	/// even the family is unrecognised.
	[[nodiscard]] static QString codecFromEssenceLabel(const QByteArray &label, const QString &fps);

	// MARK: - Shared with the MDB producer
	//
	// The msmMMOB.mdb database holds the same raw facts the MXF header does
	// (dims, layout, rates, essence label, durations). These three statics
	// are the ONE place each derived value is computed, so whichever source
	// filled the struct, the table shows the same codec name, fps label,
	// bit-depth label, resolution and audio timecode base.

	/// Raw fields → display facts: resolution + validity, codec name (with the
	/// DV i/p(PAL/NTSC) suffix), audio duration/timecode base, 1088→1080.
	/// parseFromBuffer calls it last; MdbParser calls it on database facts.
	static void finalise(MxfMetadata &meta);

	/// Sample-rate rational (tag 0x3001 / OMFI:MDFL:SampleRate): audio →
	/// `sampleRate`; video → the `fps` label via the ±0.01 speed matcher
	/// (30000/1001, 60000/2002 and 2997/100 all read "29.97") + `timecodeBase`.
	static void applyEditRate(MxfMetadata &out, quint32 num, quint32 den);

	/// Quant-bits display: 254 is Avid's float/2.14 sentinel → "Float".
	[[nodiscard]] static QString bitDepthLabel(quint32 bits);

private:
	[[nodiscard]] static MxfMetadata parseFromBuffer(const QByteArray &data);
	[[nodiscard]] static qint64 readBerLength(const QByteArray &data, qint64 offset,
											  int &bytesUsed);
	[[nodiscard]] static quint16 readUint16BE(const QByteArray &data, qint64 offset);
	[[nodiscard]] static quint32 readUint32BE(const QByteArray &data, qint64 offset);

	static void parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length,
								   MxfMetadata &out);
	/// Handles both MaterialPackage (0x36) and SourcePackage (0x37) sets. The
	/// MaterialPackage is authoritative for clip name and UMID; a SourcePackage
	/// only supplies them as a fallback when still unset. The flag lets the
	/// material values win regardless of the packages' byte order in the header.
	static void parsePackage(const QByteArray &data, qint64 startPos, qint64 length,
							 MxfMetadata &out, bool isMaterialPackage);
	static void parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length,
										 MxfMetadata &out);
	/// AAF TaggedValue set (0x3F): Name (0x5001, UTF-16BE) + Value (0x5003,
	/// an Indirect: type AUID + payload). Only four names are read —
	/// `UNC Path`, `Video`, `_IMPORTSETTING`, `_PJ`/`PROJNAME` — see
	/// MxfMetadata::sourceFilePath.
	static void parseTaggedValue(const QByteArray &data, qint64 startPos, qint64 length,
								 MxfMetadata &out);
};