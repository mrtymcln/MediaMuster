#include "mediametadata.h"
#include "logcategories.h"

#include <QHash>
#include <iterator>
#include <limits>

/// Sound essence identified from the essence label alone, for files
/// whose audio-ness never appears in a descriptor set. Two registered
/// forms, and one trap between them:
///
///  - SMPTE sound CODING node, bytes 8-9 = 04 02 (picture coding is
///    04 01). Unambiguous; covers MP2's compression UL.
///  - MXF Generic Container AES3/BWF sound MAPPINGS in the 0D
///    namespace: bytes 12-13 = 02 06, byte 14 = the wrapping variant,
///    byte 15 = 00.
///
/// That trailing 00 is load-bearing, not decoration. Avid reuses the
/// very same 0D…02 06 prefix for its PRIVATE DNxHD PICTURE labels —
/// …02060101 (SQ), …02060201 (HQ), …02060202 (HQX), …02060301 (LB) —
/// which end 01 or 02 where the registered sound wrappings end 00.
/// Without the byte-15 test those four legacy video codecs classify as
/// audio, which also zeroes their duration in the post-processing
/// below. tst_mxfparser::essence_label_audio_classification pins every
/// one of these six colliding ULs.
static bool isAudioCompressionLabel(const QByteArray &label)
{
	if (label.size() < 16)
		return false;
	const auto b8 = static_cast<quint8>(label[8]);
	const auto b9 = static_cast<quint8>(label[9]);
	if (b8 == 0x04 && b9 == 0x02)
		return true;
	return b8 == 0x0D && static_cast<quint8>(label[12]) == 0x02 &&
		   static_cast<quint8>(label[13]) == 0x06 && static_cast<quint8>(label[15]) == 0x00;
}

/// Quant-bits display. 254 is Avid's sentinel for the non-integer
/// DNxUncompressed formats — 32-bit float and 16-bit 2.14 fixed point,
/// whose descriptors are byte-identical (verified against real files),
/// so they share one label. "254-bit" is not a bit depth.
QString MediaMetadataUtil::bitDepthLabel(quint32 bits)
{
	if (bits == 254)
		return QStringLiteral("Float");
	return QStringLiteral("%1-bit").arg(bits);
}

// MARK: - Shared metadata derivation

/// Derive display facts from the raw values supplied by any media reader.
void MediaMetadataUtil::finalise(MediaMetadata &meta)
{
	// Audio-ness can be visible in the essence label alone, with no
	// sound descriptor set in the header. Decide it FIRST — the duration
	// derivation, the validity rule, and the codec fallback below all
	// branch on isAudio. (This used to leak into the scanner as a
	// compare against the display name "PCM Audio", which covered only
	// PCM and would have broken silently on a codec rebrand.)
	if (!meta.isAudio && isAudioCompressionLabel(meta.compressionLabel))
		meta.isAudio = true;

	// Graph-based MXF parsing supplies the selected file's duration already
	// converted to display frames. MDB/OMF readers likewise derive frames
	// from the descriptor and owning mob's rate. Only graphless legacy
	// recovery infers an audio timecode base from the structural frame count
	// and descriptor sample count; an unresolvable rate stays blank.
	if (meta.isAudio)
	{
		const qint64 frames = meta.durationFrames;
		const qint64 samples = meta.descriptorDuration; // WAVE ContainerDuration
		if (meta.timecodeBase <= 0 && frames > 0 && samples > 0 && meta.sampleRate > 0)
		{
			const double base = double(frames) * meta.sampleRate / double(samples);
			// Bounds mirrored in MediaFile::effectiveTimecodeBase.
			if (base >= 1.0 && base < 1000.0)
				meta.timecodeBase = qRound(base);
		}
		if (meta.timecodeBase <= 0)
			meta.durationFrames = 0;
	}
	else if (!meta.durationFromTrack && meta.descriptorDuration > 0 &&
			 (meta.durationFrames == 0 || meta.descriptorDuration < meta.durationFrames))
		meta.durationFrames = meta.descriptorDuration;

	// Interlaced sources store one field height in the descriptor (e.g. 540
	// for 1080i), so a field height doubles to the full frame. Only layout 1
	// (Separate Fields) is a half height: Avid's own raster filters pair the
	// half heights {540,544} with layouts {1,4} and the FULL heights
	// {1080,1088} with layouts {2,3}, so layout 3 must NOT be doubled — it
	// was, and five corpus files reported 1920x2160.
	// (Layout 4 is deliberately not added: Avid's 4K filter groups it with
	// the full heights, contradicting its own 1080i filter, and no file in
	// the corpus uses it. Left alone until a real file settles it.)
	// Source: SupportingFiles/DynamicRelinkUI/DRUI.xml, cbxRaster filters.
	// A producer that already normalised to the full frame says so with
	// heightIsFrameHeight (the MDB stores half heights for layouts 1 AND 3
	// and doubles them itself before handing over).
	if (meta.frameLayout == 1 && !meta.heightIsFrameHeight)
	{
		meta.height = meta.height > 0 && meta.height <= std::numeric_limits<int>::max() / 2
						  ? meta.height * 2
						  : 0;
		meta.heightIsFrameHeight = true;
	}

	// Avid pads these two rasters for macroblock alignment and then treats
	// the padded height as the real one everywhere it matters — its relink
	// ranking runs heights through `norm(h)` (1088 -> 1080, 544 -> 540), and
	// it writes the normalised value into SampledHeight in the file itself.
	// Without this the table shows 1920x1088, splitting clips that Media
	// Composer, the Media Tool and every EDL agree are 1080.
	// Source: DRUI.xml SortingText, "Highest Quality" relink method.
	if (meta.height == 1088)
		meta.height = 1080;
	else if (meta.height == 544)
		meta.height = 540;

	// Both producers carry the raster in full: the MXF picture descriptor
	// must hold StoredWidth/StoredHeight (tags 0x3203/0x3202), and the MDB
	// holds OMFI:DIDD:StoredWidth/StoredHeight. A file with a height and no
	// usable width therefore reports no resolution rather than a guessed one
	// - the same rule the bin and the clip name follow.
	if (meta.width > 0 && meta.height > 0)
	{
		meta.resolution = QStringLiteral("%1x%2").arg(meta.width).arg(meta.height);
		meta.valid = true;
	}
	else if (meta.isAudio &&
			 (meta.sampleRate > 0 || isAudioCompressionLabel(meta.compressionLabel)))
	{
		// Descriptor-audio proves itself with a sample rate; label-only
		// audio is vouched for by the UL. Either way the file is real
		// audio essence — codec named, duration honestly blank when the
		// header offers no way to derive one.
		meta.valid = true;
	}

	// Codec lookup is deferred until here so the framerate has been
	// finalised; DNxHD bitrate names depend on fps.
	if (meta.valid && meta.codec.isEmpty() && !meta.compressionLabel.isEmpty())
		meta.codec = codecFromCompressionLabel(meta.compressionLabel, meta.fps);
	if (meta.valid && meta.codec.isEmpty() && meta.isAudio && meta.pcmDescriptor)
		meta.codec = QString::fromLatin1(kPcmAudioName);

	// Avid displays DV as 'DV 25 420 i(PAL)' etc. Scan type and
	// broadcast standard come from MXF metadata (frame layout + fps
	// + height) rather than the codec UL itself. Skipped when the name
	// already states them — Avid's own config names do ("DV PAL 25Mbps
	// 4:1:1", "DV 1080 50i"), and appending would print them twice.
	const bool dvAlreadyQualified =
		meta.codec.contains(QLatin1String("PAL")) || meta.codec.contains(QLatin1String("NTSC")) ||
		(meta.codec.size() >= 2 &&
		 (meta.codec.back() == QLatin1Char('i') || meta.codec.back() == QLatin1Char('p')) &&
		 meta.codec.at(meta.codec.size() - 2).isDigit());
	if (meta.codec.startsWith(QLatin1String("DV ")) && !dvAlreadyQualified)
	{
		const QString scan = (meta.frameLayout == 1 || meta.frameLayout == 3) ? QStringLiteral("i")
																			  : QStringLiteral("p");
		QString standard;
		if (meta.fps == QLatin1String("25") || meta.height == 576 || meta.height == 288)
			standard = QStringLiteral("PAL");
		else if (meta.fps == QLatin1String("29.97") || meta.height == 480 || meta.height == 486)
			standard = QStringLiteral("NTSC");
		if (!standard.isEmpty())
			meta.codec += QStringLiteral(" %1(%2)").arg(scan, standard);
	}
}

// MARK: - Shared derivations

void MediaMetadataUtil::applyEditRate(MediaMetadata &out, quint32 num, quint32 den)
{
	if (den == 0 || num == 0 || num > quint32(std::numeric_limits<qint32>::max()) ||
		den > quint32(std::numeric_limits<qint32>::max()))
		return;
	if (out.isAudio)
	{
		out.sampleRate = static_cast<int>(num / den);
	}
	else
	{
		// Rates are judged by the SPEED the fraction works
		// out to, never by its exact digits: the same 29.97
		// arrives as 30000/1001 (Avid), 60000/2002
		// (unreduced), or 2997/100 (decimal-approximation
		// muxers), and an exact-digit whitelist silently
		// rounded everything but the first to "30". The five
		// labels are the complete fractional set Media
		// Composer can produce (47.952 since v8.3, 119.88
		// since 2018.7). The ±0.01 windows can't collide:
		// the closest fractional/integer pair (23.976 vs 24)
		// is 0.024 apart, and any decimal approximation of a
		// true 1000/1001 rate lands within 0.004 of it.
		struct FractionalRate
		{
			double value;
			const char *label;
		};
		static constexpr FractionalRate kFractional[] = {
			{24000.0 / 1001.0, "23.976"},  {30000.0 / 1001.0, "29.97"},
			{48000.0 / 1001.0, "47.952"},  {60000.0 / 1001.0, "59.94"},
			{120000.0 / 1001.0, "119.88"},
		};
		const double rate = double(num) / double(den);
		const char *label = nullptr;
		for (const auto &k : kFractional)
		{
			if (qAbs(rate - k.value) < 0.01)
			{
				label = k.label;
				break;
			}
		}
		if (label)
			out.fps = QLatin1String(label);
		else if (rate < 1000.0 && qAbs(rate - qRound(rate)) < 0.01)
			out.fps = QString::number(qRound(rate));
		else
			// No known family: show the real value. Rounding
			// an unrecognised rate to a neighbouring integer
			// is a wrong answer delivered with no warning.
			out.fps = QString::number(rate, 'g', 6);

		// Nominal base for timecode duration rendering
		// (23.976 counts in base 24, 29.97 in base 30...).
		// Bounds mirrored in MediaFile::effectiveTimecodeBase.
		if (rate >= 1.0 && rate < 1000.0)
			out.timecodeBase = qRound(rate);
	}
}

// MARK: - Codec UL lookup

/// Maps compression/coding ULs to display codec names. Tested against real files
/// from MC 2025.12 –– see tst_mxfparser::real_avid_headers_parse_exactly.
QString MediaMetadataUtil::codecFromCompressionLabel(const QByteArray &label, const QString &fps)
{
	if (label.isEmpty())
		return {};

	// Hex strings decoded once at first call; lookup compares raw
	// bytes against `label`, no per-call toHex/toUpper allocation.
	struct Entry
	{
		const char *hex;
		const char *name;
	};
	// MARK: - New dictionary
	//
	// Where the codec branding differs from the internal name, that string
	// is kept in a 'UI' comment beside it.
	// DNx is the exception: these strings are routing keys into kDnxTiers,
	// which surfaces the legacy branding.
	//
	// Avid stores the labels half swapped. To rebuild one:
	//   group1 = LE 32-bit, groups 2-3 = LE 16-bit, then 8 plain bytes.
	//   '342B0E06-0104-0A01-04-01-02-02-71-03-00-00'
	//      -> 060E2B34 040101 0A 04010202 71 03 0000
	static constexpr Entry kEntries[] = {
		// MARK: Core entries  (Avid config names; UI name in comment)
		{"060E2B34040101010D01030102050101", "Avid 1:1 8-bit"},
		{"060E2B34040101010D01030102050201", "Avid 1:1 10-bit"},
		{"060E2B34040101010D01030102010201", "Avid 2:1"},
		{"060E2B34040101010D01030102010401", "Avid 3:1"},
		{"060E2B34040101010D01030102010101", "Avid 15:1s"},
		{"060E2B34040101010E04020102040100", "Avid 20:1"},
		{"060E2B34040101010D01030102060301", "DNxHD LB"},
		{"060E2B34040101010D01030102060101", "DNxHD SQ"},
		{"060E2B34040101010D01030102060201", "DNxHD HQ"},
		{"060E2B34040101010D01030102060202", "DNxHD HQX"},
		{"060E2B340401010A0401020271130000", "DNxHD LB"},        // Avid config also: "Avid DNx LB"
		{"060E2B340401010A0401020271030000", "DNxHD SQ"},        // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271040000", "DNxHD HQ"},        // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271010000", "DNxHD HQX"},       // Avid config also: "Avid DNx HQX"
		{"060E2B340401010A0401020271070000", "DNxHD HQX"},       // Avid config also: "Avid DNx HQX"
		{"060E2B340401010A0401020271080000", "DNxHD SQ"},        // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271090000", "DNxHD HQ"},        // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271120000", "DNxHD SQ (720p)"}, // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271110000", "DNxHD HQ (720p)"}, // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271100000",
		 "DNxHD HQX (720p)"}, // Avid config also: "Avid DNx HQX"
		{"060E2B34040101010D01030102110101", "DNxHR LB"},
		{"060E2B34040101010D01030102110201", "DNxHR SQ"},
		{"060E2B34040101010D01030102110301", "DNxHR HQ"},
		{"060E2B34040101010D01030102110401", "DNxHR HQX"},
		{"060E2B34040101010D01030102110501", "DNxHR 444"},
		{"060E2B34040101010E04020102110300", "Apple ProRes 422"},
		{"060E2B34040101010E04020102110400", "Apple ProRes HQ"},
		{"060E2B340401010D0401020271250000", "DNxHR HQX"}, // Avid config also: "Avid DNx HQX"
		{"060E2B340401010D0401020271260000", "DNxHR HQ"},  // Avid config also: "Avid DNx HQ"
		{"060E2B340401010D0401020271270000", "DNxHR SQ"},  // Avid config also: "Avid DNx SQ"
		{"060E2B340401010D0401020271280000", "DNxHR LB"},  // Avid config also: "Avid DNx LB"
		{"060E2B340401010D0401020203070100", "Avid DNxUncompressed"},
		{"060E2B340401010D0401020203070200", "Avid DNxUncompressed 2.14"},
		{"060E2B34040101010401020201020101", "Sony IMX 30"},
		{"060E2B34040101010401020201020102", "Sony IMX 40"},
		{"060E2B34040101010401020201020103", "Sony IMX 50"},
		{"060E2B34040101030401020201030300", "XDCAM EX 35"},
		{"060E2B34040101030401020201040200", "XDCAM EX 35"},
		{"060E2B34040101030401020201040300", "XDCAM HD 50"},
		{"060E2B340401010A0401020201323101", "AVC Intra"}, // was: "AVC/XAVC Intra 100"
		{"060E2B340401010A0401020201323102", "AVC Intra"}, // was: "AVC/XAVC Intra 100"
		{"060E2B340401010A0401020201323103", "AVC Intra"}, // was: "AVC/XAVC Intra 100"
		{"060E2B340401010A0401020201323108", "AVC Intra"}, // was: "AVC/XAVC Intra 100"
		{"060E2B340401010D0401020201323201", "AVC Intra"}, // was: "XAVC HD Intra CBG Class 200"
		{"060E2B340401010D0401020201323202", "AVC Intra"}, // was: "XAVC HD Intra CBG Class 200"
		{"060E2B340401010D0401020201323203", "AVC Intra"}, // was: "XAVC HD Intra CBG Class 200"
		{"060E2B340401010D0401020201323204", "AVC Intra"}, // was: "XAVC HD Intra CBG Class 200"
		{"060E2B340401010A0401020201313001", "AVC Intra"}, // was: "AVC-Intra 50"
		{"060E2B340401010A0401020201323001", "AVC Intra"}, // was: "AVC-Intra 4:2:2"
		{"060E2B340401010A0401020201323104", "AVC Intra"}, // was: "AVC-Intra 100 (4:2:2)"
		{"060E2B340401010A0401020201323109", "AVC Intra"}, // was: "AVC-Intra 100"
		{"060E2B340401010D0401020201312001", "AVC Long GOP"},
		{"060E2B340401010D0401020201314001", "AVC Long GOP"},
		{"060E2B340401010D0401020201316001", "AVC Long GOP"},
		{"060E2B340401010D0401020201311101", "H.264"}, // was: "H.264 Proxy"
		{"060E2B34040101010401020202010200", "DV 25 420"},
		{"060E2B34040101010401020202020200", "DV PAL 25Mbps 4:1:1"},  // UI: "DV 25 411"
		{"060E2B34040101010401020202020400", "DV PAL 50Mbps 4:2:2"},  // UI: "DV 50"
		{"060E2B34040101010401020202020100", "DV NTSC 25Mbps 4:1:1"}, // UI: "DV 25 411"
		{"060E2B34040101010401020202020300", "DV NTSC 50Mbps 4:2:2"}, // UI: "DV 50"
		{"060E2B34040101010401020202020500", "DV 1080 60i"},          // UI: "DVCPro HD"
		{"060E2B34040101010401020202020600", "DV 1080 50i"},          // UI: "DVCPro HD"
		{"060E2B34040101070401020203010100", "J2K HD"},               // UI: "JPEG 2000"
		{"060E2B340401010D040102020301020A", "JPEG 2000 IMF"},
		{"060E2B340401010D0401020203010312", "JPEG 2000 IMF"},
		{"060E2B34040101010D010301020C0101", "Apple ProRes Proxy"},
		{"060E2B34040101010D010301020C0201", "Apple ProRes LT"},
		{"060E2B34040101010D010301020C0301", "Apple ProRes 422"},
		{"060E2B34040101010D010301020C0401", "Apple ProRes HQ"},
		{"060E2B34040101010D010301020C0501", "Apple ProRes 4444"},
		{"060E2B340401010D0401020203060100", "Apple ProRes Proxy"},
		{"060E2B340401010D0401020203060200", "Apple ProRes LT"},
		{"060E2B340401010D0401020203060300", "Apple ProRes 422"},
		{"060E2B340401010D0401020203060400", "Apple ProRes HQ"},
		{"060E2B340401010D0401020203060500", "Apple ProRes 4444"},
		{"060E2B340401010D0401020203060600", "Apple ProRes 4444 XQ"},
		{"060E2B34040101010E04030101030200", "Avid Title/Matte"},
		{"4B464141000D4D4F", "Avid Title (Uncompressed)"},
		{"060E2B34040101010D01030102060100", kPcmAudioName},
		{"060E2B34040101010402020203020500", "MP2"}, // UI: "MP2 Audio"

		// MARK: DNx additions from Avid MC 25.12  (version byte 0D, not 0A)
		{"060E2B340401010D04010202710A0000", "Avid DNx TR"},  // vcid 1244  1440x540i
		{"060E2B340401010D0401020271160000", "Avid DNx 444"}, // vcid 1256  1920x1080p
		{"060E2B340401010D0401020271180000", "Avid DNx TR"},  // vcid 1258  960x720p
		{"060E2B340401010D0401020271190000", "Avid DNx TR"},  // vcid 1259  1440x1080p
		{"060E2B340401010D04010202711A0000", "Avid DNx TR"},  // vcid 1260  decode only
		{"060E2B340401010D0401020271240000", "Avid DNx 444"}, // vcid 1270  DNxHR 444

		// MARK: unverified against any corpus
		{"060E2B34040101010401020202020700", "DV 720 60p"},
		{"060E2B34040101010401020202020800", "DV 720 50p"},
		{"060E2B34040101010402020203020100", "AC-3"},
		{"060E2B34040101010E04020102030105", "AVC Intra"}, // was: "AVCI SD"
		{"060E2B34040101010E04020102050100", "JPEG Still Image"},
		{"060E2B34040101010E04020102050201", "Motion-JPEG"},
		{"060E2B34040101010E04020102090000", "Avid Packed"},
		{"060E2B34040101010E04020102090500", "RLE Alpha 8bit"},
		{"060E2B34040101010E04020102090600", "RLE Alpha 10bit"},
		{"060E2B34040101010E04020102100000", "1:1 RGB 10bit"},
		{"060E2B34040101030E04410101030000", "DNxRLE Alpha"},
		{"060E2B34040101030E04410101050200", "Avid DNx HQ"},
		{"060E2B34040101030E04410101050300", "Avid DNx SQ"},
		{"060E2B34040101030E04410101060100", "Avid DNxStitched AVC-I 4:2:2"},
		{"060E2B34040101030E04410103010000", "OpenEXR"},
		{"060E2B34040101030E04410104010000", "Tiff"},
		{"060E2B34040101030E04410105010000", "PNG"},
		{"060E2B34040101030E04410106010000", "DPX"},
		{"060E2B34040101030E04410107010000", "TGA"},
		{"060E2B34040101030E0441010A010100", "H.263"},
		{"060E2B34040101030E04410202010000", "ProRes RAW"},
		{"060E2B34040101030E04410202020000", "ProRes RAW HQ"},
		{"060E2B34040101030E04411001010000", "Apple Lossless"},
		{"060E2B34040101090401020203010104", "J2K DCP 4K 24fps"},
		{"060E2B340401010A0401020101020201", "AJA Xena v210"},
		{"060E2B340401010A0402020101000000", "AES3"},
		{"060E2B340401010C0E15000500012000", "H.265/HEVC Main 10 Profile"},
		{"060E2B340401010D0401020201311001", "AVC Long GOP"}, // was: "AVC Long-GOP Baseline"
		{"060E2B340401010D0401020201313001", "AVC Long GOP"}, // was: "AVC Long-GOP Extended"
		{"060E2B340401010D0401020201315001", "AVC Long GOP"}, // was: "AVC Long-GOP High10"
		{"060E2B340401010D0401020201325001", "AVC Intra"},    // was: "AVC-Intra 4:4:4"
		{"060E2B340401010D0401020201412001", "H.265/HEVC Main 10 Profile"},
		{"060E2B340401010D0401020201413001", "H.265/HEVC Main 12 Profile"},
		{"060E2B340401010D0401020201431001", "H.265/HEVC Main 4:4:4 Profile"},
		{"060E2B340401010D0401020201432001", "H.265/HEVC Main 4:4:4 10 Profile"},
		{"060E2B340401010D0401020201433001", "H.265/HEVC Main 4:4:4 12 Profile"},
		{"060E2B340401010D0401020201443001", "H.265/HEVC Main 12 Intra Profile"},
		{"060E2B340401010D0401020201461001", "H.265/HEVC Main 4:4:4 Intra Profile"},
		{"060E2B340401010D0401020201462001", "H.265/HEVC Main 4:4:4 10 Intra Profile"},
		{"060E2B340401010D0401020201463001", "H.265/HEVC Main 4:4:4 12 Intra Profile"},
		{"060E2B340401010D0401020203040100", "ACES 2065 MXF"},
		{"060E2B340401010D0401020203040200", "ACES 2065 MXF"},
		{"060E2B340401010D0402020204010200", "MP2"},
		{"060E2B340401010D0402020204010300", "MP3"},
		{"060E2B340401010D0402020204020300", "MP3"},
	};

	static const QHash<QByteArray, QString> kCodecs = []
	{
		QHash<QByteArray, QString> m;
		m.reserve(std::size(kEntries));
		for (const auto &e : kEntries)
			m.insert(QByteArray::fromHex(e.hex), QString::fromLatin1(e.name));
		return m;
	}();

	const auto it = kCodecs.find(label);
	if (it == kCodecs.end())
	{
		// Unknown UL. Build the hex string only on the error path;
		// toHex/toUpper isn't free, and we don't want it firing for
		// every known-codec lookup.
		const QString hexStr = QString::fromLatin1(label.toHex().toUpper());

		// Try to return the family name from the byte structure,
		// so editors see something more useful than the raw hex.
		QString family;
		if (label.size() >= 16)
		{
			const quint8 b8 = static_cast<quint8>(label[8]);
			const quint8 b9 = static_cast<quint8>(label[9]);
			const quint8 b12 = static_cast<quint8>(label[12]);

			if (b8 == 0x04 && b9 == 0x01) // SMPTE picture
			{
				switch (b12)
				{
				case 0x01:
					family = QStringLiteral("MPEG");
					break;
				case 0x02:
					family = QStringLiteral("DV");
					break;
				case 0x03:
					family = QStringLiteral("Picture Coding");
					break; // J2K, ProRes, FFV1
				case 0x71:
					family = QStringLiteral("VC-3");
					break; // DNxHD / DNxHR
				}
			}
			else if (b8 == 0x04 && b9 == 0x02)
			{
				family = QStringLiteral("Audio");
			}
			else if (b8 == 0x0E && b9 == 0x04)
			{
				family = QStringLiteral("Avid");
			}
			else if (b8 == 0x0D)
				family = QStringLiteral("Organisationally registered");
		}
		qCDebug(lcMetadata) << "unrecognised compression label" << hexStr << "family:" << family;
		if (!family.isEmpty())
			return family + QStringLiteral(" (unknown variant: ") + hexStr + QLatin1Char(')');
		return QStringLiteral("Unknown (") + hexStr + QLatin1Char(')');
	}

	const QString &baseName = it.value();

	// DNxHD naming has two layers. The table above keys each UL by its
	// TECHNICAL identity (the engineering tier), and this table resolves
	// the display form: Avid's current branding ("Avid DNx <level>", per
	// their 2025 whitepaper) leading, with the legacy name - which depends
	// on frame rate AND raster — kept in the parenthesis. An empty bitrate
	// cell means the whitepapers don't document that rate/raster combination;
	// the brand shows bare rather than invent a number.
	// 1080 rates: 2012 whitepaper p9; 720p rates: p10 (75 at 29.97; 60 at 25 and 23.976).
	struct DnxEntry
	{
		const char *technical, *brand, *r30, *r25, *r50, *r60, *r24;
	};
	// Avid DNxHD Technology whitepaper (2012), pp9–10. The 720p
	// 50/59.94 rows begin at the bottom of p9, before the p10 continuation.
	static constexpr DnxEntry kDnxTiers[] = {
		// technical, brand, 29.97/30, 25, 50, 59.94/60, 23.976/24
		{"DNxHD LB", "Avid DNx LB", "45", "36", "75", "90", "36"},
		{"DNxHD SQ", "Avid DNx SQ", "145", "120", "240", "290", "115"},
		{"DNxHD HQ", "Avid DNx HQ", "220", "185", "365", "440", "175"},
		{"DNxHD HQX", "Avid DNx HQX", "220X", "185X", "365X", "440X", "175X"},
		{"DNxHD SQ (720p)", "Avid DNx SQ", "75", "60", "115", "145", "60"},
		{"DNxHD HQ (720p)", "Avid DNx HQ", "110", "90", "175", "220", "90"},
		{"DNxHD HQX (720p)", "Avid DNx HQX", "110x", "90x", "175x", "220x", "90x"},
		{"DNxHR LB", "Avid DNx LB", "", "", "", "", ""},
		{"DNxHR SQ", "Avid DNx SQ", "", "", "", "", ""},
		{"DNxHR HQ", "Avid DNx HQ", "", "", "", "", ""},
		{"DNxHR HQX", "Avid DNx HQX", "", "", "", "", ""},
		{"DNxHR 444", "Avid DNx 444", "", "", "", "", ""},
	};

	for (const auto &e : kDnxTiers)
	{
		if (baseName != QLatin1String(e.technical))
			continue;
		const char *bitrate;
		if (fps == QLatin1String("29.97") || fps == QLatin1String("30"))
			bitrate = e.r30;
		else if (fps == QLatin1String("25"))
			bitrate = e.r25;
		else if (fps == QLatin1String("50"))
			bitrate = e.r50;
		else if (fps == QLatin1String("59.94") || fps == QLatin1String("60"))
			bitrate = e.r60;
		else if (fps == QLatin1String("23.976") || fps == QLatin1String("24"))
			bitrate = e.r24;
		else
			bitrate = ""; // unknown/unsupported rate cannot establish a bitrate name
		if (bitrate[0] == '\0')
			return QLatin1String(e.brand);
		return QLatin1String(e.brand) + QLatin1String(" (DNxHD ") + QLatin1String(bitrate) +
			   QLatin1Char(')');
	}

	return baseName;
}

QString MediaMetadataUtil::sourceFileBaseName(const QString &path)
{
	const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
	return slash < 0 ? path : path.mid(slash + 1);
}
