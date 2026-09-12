#pragma once

#include "avidprecompute.h"

#include <QByteArray>
#include <QString>

// MARK: - Shared codec names

/// Shared PCM display name. Classification uses descriptor/label evidence,
/// never this presentation string.
inline constexpr char kPcmAudioName[] = "PCM";

// MARK: - MediaMetadata

/// Shared metadata assembled from MXF headers, OMF files or Avid databases.
/// This is MediaMuster's aggregate, not a serialized Avid object class.
/// `valid=false` means technical facts could not be established reliably.
/// Classification has its own validity flag. Empty strings mean unknown;
/// HeaderStatus distinguishes malformed/incomplete data from a completed read.
struct MediaMetadata
{
	enum class HeaderStatus
	{
		NotRead,
		Complete,
		Incomplete,
		Malformed,
		IoError,
		LimitExceeded
	};
	HeaderStatus headerStatus = HeaderStatus::NotRead;
	QString fileMobId; ///< Owning file SourcePackage, distinct from the material/master UMID.
	bool pcmDescriptor =
		false; ///< A Wave/AES3/legacy PCM descriptor establishes PCM when coding is absent.
	bool rgbaDescriptor =
		false; ///< The selected essence descriptor is RGBA, not another picture class.
	bool rgbaAlpha8 = false; ///< Its complete pixel layout describes only an 8-bit alpha component.
	bool pictureCodingPresent = false; ///< A present but unusable coding property must not become
									   ///< an absent-property fallback.
	bool hasMaterialPackage =
		false; ///< A completed header selected a logical master identity (MaterialPackage in MXF),
			   ///< independently of usage classification.
	bool classificationKnown =
		false;          ///< An identified material/master package supplied a usage verdict.
	QString codec;      ///< Resolved codec name, e.g. 'Avid DNx HQ (DNxHD 220)'.
	QString resolution; ///< '1920x1080', or empty for audio.
	QString fps;        ///< '23.976', '25', '29.97', etc.
	QString bitDepth;   ///< '8-bit', '10-bit', '24-bit'.
	QString
		umid; ///< Canonical hex UMID from tag 0x4401 (MaterialPackage, or SourcePackage fallback).
	QString clipName; ///< Clip name from tag 0x4402 in the material package.

	/// True when clipName came from the MaterialPackage (the master clip —
	/// what Avid/MediaInfo display) rather than a SourcePackage fallback
	/// (tape/file source). Only a material name is a rung of the clip-name
	/// ladder (see MediaFile::ClipNameSource); a source name is not.
	bool clipNameFromMaterial = false;

	/// A verified master usage1 identifies a precompute. Standard LowerLevel
	/// alone is ambiguous (Avid also uses it for group/motion clips). Consult
	/// classificationKnown before interpreting false as ordinary media.
	bool isPrecompute = false;
	AvidPrecompute::Category precomputeCategory = AvidPrecompute::Category::Unknown;

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

	QByteArray compressionLabel; ///< Compression/coding UL; distinct from the file wrapping label.
	QByteArray
		wrappingLabel; ///< FileDescriptor EssenceContainer (0x3004); separate from compression.
	int width = 0;
	int height = 0;     ///< Stored value; interlaced files store one field height.
	int channels = 0;   ///< Audio only.
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
	/// the Avid-bin timecode model. 0 = unknown.
	qint64 durationFrames = 0;
	bool durationFromTrack =
		false; ///< Top-level owning-track duration, already converted to display frames.

	/// Nominal timecode base (24, 25, 30...). Video: from the 0x3001 edit
	/// rate. Audio: derived from the frame-track duration against the WAVE
	/// sample count when available. 0 = underivable.
	int timecodeBase = 0;

	/// A timecode component (set 0x14) carried DropFrame=true (tag 0x1503).
	bool dropFrame = false;

	/// Duration from the essence descriptor's ContainerDuration (tag 0x3002),
	/// in the descriptor's edit units — frames for video, samples for audio.
	/// Collected separately from the structural-component durations because
	/// an audio header mixes units; the producer resolves them into
	/// `durationFrames` using the owning track's rate.
	qint64 descriptorDuration = 0;

	bool isAudio = false;
	bool valid = false;
};

namespace MediaMetadataUtil
{
/// Derive codec, resolution, validity and duration display facts from raw metadata.
/// Shared by all producers so a database and a file header use the same rules.
void finalise(MediaMetadata &metadata);

/// Apply a positive sample/edit-rate rational: audio Hz or video fps/timecode base.
void applyEditRate(MediaMetadata &metadata, quint32 numerator, quint32 denominator);

/// Avid's quantization sentinel 254 is displayed as Float.
[[nodiscard]] QString bitDepthLabel(quint32 bits);

/// Resolve a compression/coding UL, including rate-dependent DNxHD names.
/// Unknown labels retain their hex value and any recognizable coding family.
[[nodiscard]] QString codecFromCompressionLabel(const QByteArray &label, const QString &fps);

/// A recorded import path may use either OS's separators, regardless of this host.
[[nodiscard]] QString sourceFileBaseName(const QString &path);
} // namespace MediaMetadataUtil
