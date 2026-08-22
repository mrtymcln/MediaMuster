#include "mxfparser.h"
#include "logging.h"
#include "mobid.h"
#include <QFile>
#include <QHash>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <unordered_map>

// Extracts technical metadata from MXF file headers via direct
// KLV parsing. Only the header partition is read; we never touch
// the essence, so scan time is independent of file size.
//
// MARK: - Anchor labels
//
// The two SMPTE Universal Labels below are the KLV keys we anchor
// against. `kUlHeaderPartition` marks where parsing starts;
// `kUlSetPrefix` flags every metadata Set we care about
// (descriptors, packages, components).

static constexpr char kUlHeaderPartition[] =
	"\x06\x0e\x2b\x34\x02\x05\x01\x01\x0d\x01\x02\x01\x01\x02";

static constexpr char kUlSetPrefix[] = "\x06\x0e\x2b\x34\x02\x53\x01\x01\x0d\x01\x01\x01\x01\x01";

// Set type bytes: the Sets differ only at byte 14 of the 16-byte UL.
// We compare the first 13 bytes against `kUlSetPrefix`, then switch
// on byte 14.
static constexpr quint8 kSetCdci = 0x28;
static constexpr quint8 kSetRgba = 0x29;
static constexpr quint8 kSetWave = 0x48;
static constexpr quint8 kSetAes3 = 0x47;
/// MPEG-flavour sound descriptor. MC 2025 writes it for MP2 audio media
/// (re-created tones); carries the same 0x3001/0x3D0x tag set as Wave,
/// plus the compression UL in 0x3D06. Unrecognised, the whole file parsed
/// invalid and its row lost every MXF-derived field.
static constexpr quint8 kSetSoundMpeg = 0x5E;
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
static bool isAudioEssenceLabel(const QByteArray &label)
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

static constexpr quint8 kSetMatPkg = 0x36;
static constexpr quint8 kSetSrcPkg = 0x37;
static constexpr quint8 kSetSequence = 0x0F;
static constexpr quint8 kSetSourceClip = 0x11;
static constexpr quint8 kSetTimecode = 0x14;
/// AAF TaggedValue — the MaterialPackage's import attributes (UNC Path,
/// Video, _IMPORTSETTING, _PJ...). See parseTaggedValue.
static constexpr quint8 kSetTaggedValue = 0x3F;

// MARK: - UsageCode (Media vs Precompute)
//
// A rendered effect is not something you can spot by its name — it is a fact
// Avid records. The MaterialPackage's UsageCode property (local tag 0x4408,
// standard AAF/MXF) carries `Usage_LowerLevel` on a render and is ABSENT on
// ordinary media. Measured over 823 distinct real files: present on all 15
// renders, on none of the other 808. Notably a video mixdown carries no
// UsageCode — correctly, since a mixdown is a master clip, not a precompute
// (Avid community thread 90606).
//
// The enum lives in the last-but-one byte: 05 SubClip, 06 AdjustedClip,
// 07 TopLevel, 08 LowerLevel, 09 Template.
//
// TODO — a second, corroborating signal exists but is not read yet.
// Avid also writes its own INTEGER usage code as a private property, UL
// a022006094eb75cb96c469924f6211d3, whose values match the OMF enumeration
// that pyavb documents (MIT, src/avb/trackgroups.py — `OMFI:MOBJ:UsageCode`):
//   1 = master mob of a precompute        (measured: all 15 renders, nothing else)
//   9 = file mob of a precompute          (measured on their source packages)
//   2 subclip, 3 effect, 4 group, 5 groupoofter, 6 motion, 7 mastermob
// It would let MediaMuster name WHY a file is generated — and tell a subclip
// or a group clip apart from plain media, which 0x4408 alone cannot. Reading
// it costs more: the tag is DYNAMIC (seen as 0xFFFA in one file), so the
// primer pack has to be parsed to map tag -> UL first, and this parser does
// not read the primer at all today. Worth doing if those distinctions ever
// earn a column; not needed for Media vs Precompute.

/// True when a 16-byte UsageCode value is `Usage_LowerLevel` — Avid's mark
/// for a rendered effect.
///
/// Accepts both byte orders on purpose. 14 of 15 real renders store the UL
/// as `060e2b34…0d01010201010800`; one stores the two 8-byte halves the other
/// way round (the AAF AUID form). Checking one ordering silently misses it.
static bool isPrecomputeUsage(const QByteArray &value)
{
	if (value.size() != 16)
		return false;
	static const QByteArray kLowerLevel =
		QByteArray::fromHex("060e2b34040101010d01010201010800");
	return value == kLowerLevel || value == kLowerLevel.mid(8) + kLowerLevel.left(8);
}

// MARK: - Byte-level helpers

/// Read a duration value: a big-endian integer whose recorded length
/// varies by MXF flavour (4 and 8 bytes in the wild; any length from 4
/// up reads exactly). A big-endian value's low bytes are the LAST bytes
/// of the field, so taking a fixed-width slice from the FRONT of a
/// longer field divides the value by 2^(8×extra) — the trailing bytes
/// are the ones that matter, and a field longer than 8 reads its
/// trailing 8 (the lead bytes are zero for anything that fits 64 bits).
/// Caller must have already bounds-checked that `pos + len` fits inside
/// the buffer.
static qint64 readDuration(const QByteArray &data, qint64 pos, quint16 len)
{
	if (len < 4)
		return -1;
	const int take = qMin<int>(len, 8);
	const auto *p = reinterpret_cast<const uchar *>(data.constData() + pos + len - take);
	qint64 v = 0;
	for (int i = 0; i < take; ++i)
		v = (v << 8) | p[i];
	return v;
}

/// Read a BER-encoded length. MXF uses BER short form (one byte,
/// high bit clear) for lengths 0–127 and BER long form (one count
/// byte plus N value bytes) for longer lengths. Sets `bytesUsed` to
/// the total number of bytes consumed; returns -1 on a malformed
/// length.
qint64 MxfParser::readBerLength(const QByteArray &data, qint64 offset, int &bytesUsed)
{
	if (offset >= data.size())
	{
		bytesUsed = 0;
		return -1;
	}
	const quint8 firstByte = static_cast<quint8>(data[offset]);
	if (firstByte < 0x80)
	{
		bytesUsed = 1;
		return firstByte;
	}
	// firstByte 0x80 is BER indefinite-length (lenBytes == 0), which is illegal
	// in MXF KLV. Treating it as "length 0" — as `firstByte & 0x7f` would —
	// silently desyncs the walk (the real value bytes get read as the next
	// key). Reject it as malformed, matching this function's -1 contract.
	const int lenBytes = firstByte & 0x7f;
	if (lenBytes == 0 || lenBytes > 8 || offset + 1 + lenBytes > data.size())
	{
		bytesUsed = 0;
		return -1;
	}
	bytesUsed = 1 + lenBytes;
	qint64 length = 0;
	for (int i = 0; i < lenBytes; ++i)
		length = (length << 8) | static_cast<quint8>(data[offset + 1 + i]);
	return length;
}

/// Quant-bits display. 254 is Avid's sentinel for the non-integer
/// DNxUncompressed formats — 32-bit float and 16-bit 2.14 fixed point,
/// whose descriptors are byte-identical (verified against real files),
/// so they share one label. "254-bit" is not a bit depth.
QString MxfParser::bitDepthLabel(quint32 bits)
{
	if (bits == 254)
		return QStringLiteral("Float");
	return QStringLiteral("%1-bit").arg(bits);
}

quint16 MxfParser::readUint16BE(const QByteArray &data, qint64 offset)
{
	if (offset + 2 > data.size())
		return 0;
	return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 MxfParser::readUint32BE(const QByteArray &data, qint64 offset)
{
	if (offset + 4 > data.size())
		return 0;
	return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

/// NUL-terminated UTF-16BE text as MXF/AAF writes package names and
/// TaggedValue names. Stops at the first NUL inside `len`.
static QString readUtf16BE(const QByteArray &data, qint64 pos, quint16 len)
{
	QString s;
	s.reserve(len / 2);
	const auto *p = reinterpret_cast<const uchar *>(data.constData());
	for (int i = 0; i + 1 < len; i += 2)
	{
		const quint16 ch = quint16((p[pos + i] << 8) | p[pos + i + 1]);
		if (ch == 0)
			break;
		s.append(QChar(ch));
	}
	return s;
}

// MARK: - Public parse entry

/// Avid allocates 256 KB or 512 KB for the header partition, depending
/// on the Media Composer version. Try the fast 256 KB read first;
/// only re-read up to 512 KB if the first pass didn't yield valid
/// metadata.
MxfMetadata MxfParser::parseHeader(const QString &filePath, qint64 *bytesRead)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcMxf) << "cannot open" << filePath << file.errorString();
		if (bytesRead)
			*bytesRead = 0;
		return {};
	}

	constexpr qint64 kFastRead = 256 * 1024;
	constexpr qint64 kFallbackRead = 512 * 1024;

	QByteArray data = file.read(kFastRead);
	MxfMetadata meta = parseFromBuffer(data);

	// Re-read up to `kFallbackRead` when the fast read wasn't enough. Two
	// triggers: nothing valid at all, or a valid parse whose package
	// identity is incomplete — Avid allocates 256 KB OR 512 KB for the
	// header depending on the MC version, so on the 512 KB flavour the
	// descriptors can land in the fast read while the MaterialPackage
	// (UMID + authoritative clip name) sits beyond it, and first-pass
	// "valid" would silently discard both. The ceiling stays
	// kFallbackRead: common 256 KB-header files complete their identity
	// in the fast read and never pay for a second one.
	const bool identityIncomplete =
		meta.umid.isEmpty() || meta.clipName.isEmpty() || !meta.clipNameFromMaterial;
	if ((!meta.valid || identityIncomplete) && data.size() < kFallbackRead &&
		data.size() < file.size())
	{
		data.append(file.read(kFallbackRead - data.size()));
		meta = parseFromBuffer(data);
	}
	file.close();

	// No usable header even after the fallback read: a truncated or non-MXF
	// file with an .mxf name. Worth flagging.
	if (!meta.valid)
		qCWarning(lcMxf) << "no usable MXF metadata in" << filePath << "(read" << data.size()
						 << "bytes)";

	if (bytesRead)
		*bytesRead = data.size();
	return meta;
}

// MARK: - KLV walk

MxfMetadata MxfParser::parseFromBuffer(const QByteArray &data)
{
	MxfMetadata meta;

	qint64 pos = 0;
	const int headerIdx = data.indexOf(QByteArray::fromRawData(kUlHeaderPartition, 14));
	if (headerIdx >= 0)
		pos = headerIdx;

	// Hot loop: memcmp the source buffer directly instead of
	// `data.mid(pos, 16)`; the mid allocations added up to ~500
	// throwaway QByteArrays per MXF × 50k+ files per scan.
	const char *base = data.constData();
	const qint64 dataSize = data.size();
	while (pos + 16 < dataSize)
	{
		int bytesUsed = 0;
		const qint64 length = readBerLength(data, pos + 16, bytesUsed);
		if (length < 0 || bytesUsed == 0)
			break;
		const qint64 valuePos = pos + 16 + bytesUsed;
		// Overflow-safe bounds check. A corrupt BER length can be up to
		// ~2^63, so `valuePos + length` would signed-overflow (UB) and wrap
		// negative, sneaking past a naive `> dataSize` test; `pos` would then
		// go negative and the next read runs off the front of the buffer.
		// Comparing against the remaining space can't overflow (both terms
		// are non-negative and valuePos <= dataSize here).
		if (valuePos > dataSize || length > dataSize - valuePos)
			break;

		if (std::memcmp(base + pos, kUlSetPrefix, 13) == 0)
		{
			const auto setType = static_cast<quint8>(base[pos + 14]);
			switch (setType)
			{
			case kSetCdci:
			case kSetRgba:
				parseDescriptorSet(data, valuePos, length, meta);
				break;
			case kSetWave:
			case kSetAes3:
			case kSetSoundMpeg:
				meta.isAudio = true;
				parseDescriptorSet(data, valuePos, length, meta);
				break;
			case kSetMatPkg:
				// MaterialPackage = the master clip; its name/UMID are the ones
				// Avid and MediaInfo show, so it's authoritative.
				parsePackage(data, valuePos, length, meta, /*isMaterialPackage=*/true);
				break;
			case kSetSrcPkg:
				// SourcePackage (tape/file source) only fills in name/UMID when
				// the MaterialPackage hasn't — a fallback, never an override.
				parsePackage(data, valuePos, length, meta, /*isMaterialPackage=*/false);
				break;
			case kSetSequence:
			case kSetSourceClip:
			// Timecode sets contribute two things: the duration (min-wins pool)
			// and the drop-frame flag (tag 0x1503). Every other tag is ignored.
			case kSetTimecode:
				parseStructuralComponent(data, valuePos, length, meta);
				break;
			case kSetTaggedValue:
				parseTaggedValue(data, valuePos, length, meta);
				break;
			}
		}
		pos = valuePos + length;
	}

	finalise(meta);
	return meta;
}

// MARK: - Post-processing (shared with the MDB producer)

/// Raw fields → display facts. parseFromBuffer calls this on what the KLV
/// walk found; MdbParser calls it on what msmMMOB.mdb holds. One function,
/// so resolution, validity, the codec name (+ DV suffix), the audio
/// timecode base and the 1088→1080 rule cannot drift between the two.
void MxfParser::finalise(MxfMetadata &meta)
{
	// Audio-ness can be visible in the essence label alone, with no
	// sound descriptor set in the header. Decide it FIRST — the duration
	// derivation, the validity rule, and the codec fallback below all
	// branch on isAudio. (This used to leak into the scanner as a
	// compare against the display name "PCM Audio", which covered only
	// PCM and would have broken silently on a codec rebrand.)
	if (!meta.isAudio && isAudioEssenceLabel(meta.essenceContainerLabel))
		meta.isAudio = true;

	// Durations arrive in mixed units. Structural components (Sequence /
	// SourceClip / Timecode, tag 0x0202) are in their own track's edit
	// units, and an audio file always carries frame-based durations too —
	// its MaterialPackage and tape SourcePackage tracks run at the project's
	// VIDEO rate — which undercut the sample count by a factor of ~2000, so
	// a naive min across everything is meaningless for audio.
	//
	// Durations render as bin timecode (frames at the edit rate) for video
	// AND audio. Audio keeps the component pool's min — frame counts always
	// undercut sample counts, so that min IS the frame-track duration — and
	// derives the rate the only way the header offers it: frames against
	// the WAVE descriptor's sample count (765 × 48000 / 1468800 = 25.000
	// exactly; 733 × 48000 / 1467466 = 23.976; verified on real corpus
	// files 2026-07-21). No derivable rate means no display — an unknown
	// stays blank, never a wall-clock guess. Video merges both pools
	// min-wins — every video duration is in frames, and the shortest is the
	// accurate one (asymmetrical files).
	if (meta.isAudio)
	{
		const qint64 frames = meta.durationFrames;		// component-pool min
		const qint64 samples = meta.descriptorDuration; // WAVE ContainerDuration
		if (frames > 0 && samples > 0 && meta.sampleRate > 0)
		{
			const double base = double(frames) * meta.sampleRate / double(samples);
			// Bounds mirrored in MediaFile::effectiveTimecodeBase.
			if (base >= 1.0 && base < 1000.0)
				meta.timecodeBase = qRound(base);
		}
		if (meta.timecodeBase <= 0)
			meta.durationFrames = 0;
	}
	else if (meta.descriptorDuration > 0 &&
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
		meta.height *= 2;

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

	// Some Avid MXFs write garbage into width tag 0x3203; zero out
	// implausible values so height-only inference can run.
	if (meta.width > 16384)
		meta.width = 0;

	if (meta.width > 0 && meta.height > 0)
	{
		meta.resolution = QStringLiteral("%1x%2").arg(meta.width).arg(meta.height);
		meta.valid = true;
	}
	else if (meta.height > 0)
	{
		// Width missing or corrupt; infer it from standard frame
		// sizes. Built once on first call, then reused.
		static const std::unordered_map<int, int> kHeightToWidth = {
			{2160, 3840}, // UHD
			{1080, 1920}, // HD (full)
			{720, 1280},  // HD (cropped)
			{576, 720},	  // PAL SD
			{486, 720},	  // NTSC SD (full)
			{480, 720},	  // NTSC SD (cropped)
		};
		const auto it = kHeightToWidth.find(meta.height);
		if (it != kHeightToWidth.end())
		{
			meta.width = it->second;
			meta.resolution = QStringLiteral("%1x%2").arg(meta.width).arg(meta.height);
			meta.valid = true;
		}
	}
	else if (meta.isAudio &&
			 (meta.sampleRate > 0 || isAudioEssenceLabel(meta.essenceContainerLabel)))
	{
		// Descriptor-audio proves itself with a sample rate; label-only
		// audio is vouched for by the UL. Either way the file is real
		// audio essence — codec named, duration honestly blank when the
		// header offers no way to derive one.
		meta.valid = true;
	}

	// Codec lookup is deferred until here so the framerate has been
	// finalised; DNxHD bitrate names depend on fps.
	if (meta.valid && !meta.essenceContainerLabel.isEmpty())
		meta.codec = codecFromEssenceLabel(meta.essenceContainerLabel, meta.fps);
	if (meta.valid && meta.codec.isEmpty())
		meta.codec = meta.isAudio ? QString::fromLatin1(kPcmAudioName)
								  : QStringLiteral("Avid Uncompressed");

	// Avid displays DV as 'DV 25 420 i(PAL)' etc. Scan type and
	// broadcast standard come from MXF metadata (frame layout + fps
	// + height) rather than the codec UL itself. Skipped when the name
	// already states them — Avid's own config names do ("DV PAL 25Mbps
	// 4:1:1", "DV 1080 50i"), and appending would print them twice.
	const bool dvAlreadyQualified =
		meta.codec.contains(QLatin1String("PAL")) || meta.codec.contains(QLatin1String("NTSC")) ||
		(meta.codec.size() >= 2 && (meta.codec.back() == QLatin1Char('i') || meta.codec.back() == QLatin1Char('p')) &&
		 meta.codec.at(meta.codec.size() - 2).isDigit());
	if (meta.codec.startsWith(QLatin1String("DV ")) && !dvAlreadyQualified)
	{
		const QString scan = (meta.frameLayout == 1 || meta.frameLayout == 3) ? QStringLiteral("i") : QStringLiteral("p");
		QString standard;
		if (meta.fps == QLatin1String("25") || meta.height == 576 || meta.height == 288)
			standard = QStringLiteral("PAL");
		else if (meta.fps == QLatin1String("29.97") || meta.height == 480 || meta.height == 486)
			standard = QStringLiteral("NTSC");
		if (!standard.isEmpty())
			meta.codec += QStringLiteral(" %1(%2)").arg(scan, standard);
	}
}

// MARK: - Per-set parsers

// MARK: - Shared derivations

void MxfParser::applyEditRate(MxfMetadata &out, quint32 num, quint32 den)
{
	if (den == 0)
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
			{24000.0 / 1001.0, "23.976"},
			{30000.0 / 1001.0, "29.97"},
			{48000.0 / 1001.0, "47.952"},
			{60000.0 / 1001.0, "59.94"},
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

void MxfParser::parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length,
								   MxfMetadata &out)
{
	qint64 pos = startPos;
	const qint64 endPos = startPos + length;

	while (pos + 4 <= endPos)
	{
		const quint16 tag = readUint16BE(data, pos);
		const quint16 len = readUint16BE(data, pos + 2);
		pos += 4;
		if (pos + len > endPos)
			break;

		// Tag numbers from SMPTE 377M / Avid extensions; inline
		// comments explain each constant.
		switch (tag)
		{
		case 0x3201: // picture essence coding UL — identifies the codec
			if (len >= 8 && out.essenceContainerLabel.isEmpty())
				out.essenceContainerLabel = data.mid(pos, len);
			break;
		case 0x3203: // stored width
			if (len >= 4)
				out.width = static_cast<int>(readUint32BE(data, pos));
			break;
		case 0x3202: // stored height — one field only for interlaced content
			if (len >= 4)
				out.height = static_cast<int>(readUint32BE(data, pos));
			break;
		case 0x320C: // frame layout: 0=full frame, 1=separate fields, 2=single field, 3=mixed
			if (len >= 1)
				out.frameLayout = static_cast<quint8>(data[pos]);
			break;
		case 0x3001: // sample rate — fps for video, Hz for audio
			if (len >= 8)
				applyEditRate(out, readUint32BE(data, pos), readUint32BE(data, pos + 4));
			break;
		case 0x3002: // container duration (4 or 8 bytes)
			// Kept apart from the structural-component durations: this one is
			// in the DESCRIPTOR's edit units (frames for video, samples for
			// audio), and the two pools only merge unit-aware in
			// parseFromBuffer's post-processing. Min-wins within the pool:
			// a container can legitimately run longer than the essence it
			// holds (asymmetrical files), so the shortest positive duration
			// is the accurate one.
			if (const qint64 d = readDuration(data, pos, len); d > 0)
				if (out.descriptorDuration == 0 || d < out.descriptorDuration)
					out.descriptorDuration = d;
			break;
		case 0x3D03: // audio sampling rate (alternate to 0x3001 for Wave/AES3 descriptors)
			if (out.isAudio && len >= 8)
			{
				const quint32 num = readUint32BE(data, pos);
				const quint32 den = readUint32BE(data, pos + 4);
				if (den > 0)
					out.sampleRate = static_cast<int>(num / den);
			}
			break;
		case 0x3301: // video quantisation bits
			if (len >= 4)
				out.bitDepth = bitDepthLabel(readUint32BE(data, pos));
			break;
		case 0x3D01: // audio quantisation bits
			if (out.isAudio && len >= 4)
				out.bitDepth = bitDepthLabel(readUint32BE(data, pos));
			break;
		case 0x3D07: // audio channel count
			if (out.isAudio && len >= 4)
				out.channels = static_cast<int>(readUint32BE(data, pos));
			break;
		case 0x3D06: // sound essence compression UL
			// PCM Wave/AES3 descriptors omit this tag (verified across the
			// fixture corpus), so capturing it can't disturb PCM files; the
			// MPEG sound descriptor carries its codec identity here rather
			// than in 0x3201. Resolved through the same kEntries lookup.
			if (out.isAudio && len >= 16 && out.essenceContainerLabel.isEmpty())
				out.essenceContainerLabel = data.mid(pos, len);
			break;
		}
		pos += len;
	}
}

void MxfParser::parsePackage(const QByteArray &data, qint64 startPos, qint64 length,
							 MxfMetadata &out, bool isMaterialPackage)
{
	qint64 pos = startPos;
	const qint64 endPos = startPos + length;

	while (pos + 4 <= endPos)
	{
		const quint16 tag = readUint16BE(data, pos);
		const quint16 len = readUint16BE(data, pos + 2);
		pos += 4;
		if (pos + len > endPos)
			break;

		// The MaterialPackage overrides; a SourcePackage only fills a gap. This
		// makes the result independent of byte order — some Avid files write a
		// tape SourcePackage before the MaterialPackage, and first-wins would
		// otherwise lock onto the tape name/UMID (the reported clip-name bug).
		if (tag == 0x4401 && len >= MobId::kRawSize && (isMaterialPackage || out.umid.isEmpty()))
		{
			// Package UID: 32 bytes, the canonical UMID. Routed through
			// `MobId::format` so the rendering matches PMR/MDB/AVB MOBs.
			out.umid =
				MobId::format(reinterpret_cast<const unsigned char *>(data.constData() + pos));
		}
		else if (tag == 0x4402 && (isMaterialPackage || out.clipName.isEmpty()))
		{
			// Package Name: UTF-16BE string, NUL-terminated within
			// the recorded length.
			QString name = readUtf16BE(data, pos, len);
			// Never clobber a good source name with an empty MaterialPackage name.
			if (!name.isEmpty())
			{
				out.clipName = std::move(name);
				out.clipNameFromMaterial = isMaterialPackage;
			}
		}
		else if (tag == 0x4408 && len == 16 && isMaterialPackage)
		{
			// UsageCode. Avid stamps a rendered effect's master package
			// Usage_LowerLevel; ordinary media carries no UsageCode at all.
			// This is how a precompute is told from media — see
			// isPrecomputeUsage() and the note above it.
			out.isPrecompute =
				isPrecomputeUsage(QByteArray(data.constData() + pos, len));
		}
		pos += len;
	}
}

void MxfParser::parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length,
										 MxfMetadata &out)
{
	qint64 pos = startPos;
	const qint64 endPos = startPos + length;

	while (pos + 4 <= endPos)
	{
		const quint16 tag = readUint16BE(data, pos);
		const quint16 len = readUint16BE(data, pos + 2);
		pos += 4;
		if (pos + len > endPos)
			break;

		if (tag == 0x0202) // component duration (4 or 8 bytes)
		{
			// Min-wins within the component pool; in the owning TRACK's edit
			// units — parseFromBuffer resolves the units per kind (for audio
			// this min IS the frame-track duration; see the note there).
			if (const qint64 d = readDuration(data, pos, len); d > 0)
				if (out.durationFrames == 0 || d < out.durationFrames)
					out.durationFrames = d;
		}
		else if (tag == 0x1503 && len >= 1)
		{
			// Timecode component drop-frame flag. Material and tape TC agree
			// on real Avid media, so any set claiming drop marks the clip.
			if (static_cast<quint8>(data[pos]) != 0)
				out.dropFrame = true;
		}
		pos += len;
	}
}

/// An AAF TaggedValue set: `Name` (0x5001, UTF-16BE) + `Value` (0x5003, an
/// AAF Indirect: 1 byte-order byte 'L'/'B', a 16-byte type AUID, then the
/// payload). Avid writes the MaterialPackage's import attributes this way.
/// Measured on the 795-file corpus: `UNC Path` holds the imported file's
/// path, `Video` its container ("QTFF"), `_IMPORTSETTING` exists on every
/// imported clip (756) and on none of the renders, tones or the mixdown.
/// (`_SRCFILE` here is an object REFERENCE, "__PortableObject", not the
/// path — the MDB's `_SRCFILE` is the path; the MXF's is `UNC Path`.)
/// Every 0x3F set in the corpus ends by ~132 KB, inside the fast read.
void MxfParser::parseTaggedValue(const QByteArray &data, qint64 startPos, qint64 length,
								 MxfMetadata &out)
{
	// The Indirect's String type, as the 'L' (little-endian) spelling Avid
	// writes; the 'B' spelling swaps the first three GUID fields.
	static const QByteArray kStringTypeLE = QByteArray::fromHex("0002100100000000060e2b3401040101");
	static const QByteArray kStringTypeBE = QByteArray::fromHex("0110020000000000060e2b3401040101");

	QString name;
	qint64 valuePos = -1;
	quint16 valueLen = 0;

	qint64 pos = startPos;
	const qint64 endPos = startPos + length;
	while (pos + 4 <= endPos)
	{
		const quint16 tag = readUint16BE(data, pos);
		const quint16 len = readUint16BE(data, pos + 2);
		pos += 4;
		if (pos + len > endPos)
			break;
		if (tag == 0x5001)
			name = readUtf16BE(data, pos, len);
		else if (tag == 0x5003)
		{
			valuePos = pos;
			valueLen = len;
		}
		pos += len;
	}

	if (name.isEmpty())
		return;
	if (name == QLatin1String("_IMPORTSETTING"))
	{
		out.hasImportSetting = true;
		return;
	}
	const bool wantPath = name == QLatin1String("UNC Path");
	const bool wantContainer = name == QLatin1String("Video");
	if (!wantPath && !wantContainer)
		return;

	// Decode the Indirect string: byte-order byte, type AUID, UTF-16 text.
	if (valuePos < 0 || valueLen < 17)
		return;
	const auto *p = reinterpret_cast<const uchar *>(data.constData() + valuePos);
	const bool little = p[0] == 'L';
	if (!little && p[0] != 'B')
		return;
	const QByteArray type(reinterpret_cast<const char *>(p + 1), 16);
	if (type != (little ? kStringTypeLE : kStringTypeBE))
		return; // an Int32 or other payload — not text

	QString text;
	text.reserve((valueLen - 17) / 2);
	for (int i = 17; i + 1 < valueLen; i += 2)
	{
		const quint16 ch = little ? quint16(p[i] | (p[i + 1] << 8)) : quint16((p[i] << 8) | p[i + 1]);
		if (ch == 0)
			break;
		text.append(QChar(ch));
	}
	if (text.isEmpty())
		return;
	if (wantPath)
		out.sourceFilePath = text;
	else
		out.sourceContainer = text;
}

// MARK: - Codec UL lookup

/// Maps essence-container ULs to display codec names. Tested against real files
/// from MC 2025.12 –– see tst_mxfparser::real_avid_headers_parse_exactly.
QString MxfParser::codecFromEssenceLabel(const QByteArray &label, const QString &fps)
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
		{"060E2B340401010A0401020271130000", "DNxHD LB"},		  // Avid config also: "Avid DNx LB"
		{"060E2B340401010A0401020271030000", "DNxHD SQ"},		  // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271040000", "DNxHD HQ"},		  // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271010000", "DNxHD HQX"},		  // Avid config also: "Avid DNx HQX"
		{"060E2B340401010A0401020271070000", "DNxHD HQX"},		  // Avid config also: "Avid DNx HQX"
		{"060E2B340401010A0401020271080000", "DNxHD SQ"},		  // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271090000", "DNxHD HQ"},		  // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271120000", "DNxHD SQ (720p)"},  // Avid config also: "Avid DNx SQ"
		{"060E2B340401010A0401020271110000", "DNxHD HQ (720p)"},  // Avid config also: "Avid DNx HQ"
		{"060E2B340401010A0401020271100000", "DNxHD HQX (720p)"}, // Avid config also: "Avid DNx HQX"
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
		{"060E2B34040101010401020202020500", "DV 1080 60i"},		  // UI: "DVCPro HD"
		{"060E2B34040101010401020202020600", "DV 1080 50i"},		  // UI: "DVCPro HD"
		{"060E2B34040101070401020203010100", "J2K HD"},				  // UI: "JPEG 2000"
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
		{"060E2B340401010D0401020201325001", "AVC Intra"},	  // was: "AVC-Intra 4:4:4"
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

	// MARK: - Marty's old dictionary. Kept for reference only.
	/*
	static constexpr Entry kOldEntries[] = {
		// Legacy
		{"060E2B34040101010D01030102050101", "Avid 1:1 8-bit"},
		{"060E2B34040101010D01030102050201", "Avid 1:1 10-bit"},
		{"060E2B34040101010D01030102010201", "Avid 2:1"},
		{"060E2B34040101010D01030102010401", "Avid 3:1"},
		{"060E2B34040101010D01030102010101", "Avid 15:1s"},
		{"060E2B34040101010E04020102040100", "Avid 20:1"},

		// DNxHD: Avid private
		{"060E2B34040101010D01030102060301", "DNxHD LB"},
		{"060E2B34040101010D01030102060101", "DNxHD SQ"},
		{"060E2B34040101010D01030102060201", "DNxHD HQ"},
		{"060E2B34040101010D01030102060202", "DNxHD HQX"},

		// DNxHD: SMPTE
		{"060E2B340401010A0401020271130000", "DNxHD LB"},  // CID 0x13
		{"060E2B340401010A0401020271030000", "DNxHD SQ"},  // CID 0x03
		{"060E2B340401010A0401020271040000", "DNxHD HQ"},  // CID 0x04
		{"060E2B340401010A0401020271010000", "DNxHD HQX"}, // CID 0x01
		{"060E2B340401010A0401020271070000", "DNxHD HQX"}, // CID 0x07
		{"060E2B340401010A0401020271080000", "DNxHD SQ"},  // CID 0x08
		{"060E2B340401010A0401020271090000", "DNxHD HQ"},  // CID 0x09

		// 720p flavours (CIDs 1250/1251/1252 — UL byte = CID minus 1234).
		// The "(720p)" suffix is a KEY, not a display name: it is
		// what routes these to the 720p rows in kDnxTiers below. Drop it and
		// they match the 1080 rows instead and render 1080 bitrates.
		{"060E2B340401010A0401020271120000", "DNxHD SQ (720p)"},
		{"060E2B340401010A0401020271110000", "DNxHD HQ (720p)"},
		{"060E2B340401010A0401020271100000", "DNxHD HQX (720p)"},

		// DNxHR: Avid private
		{"060E2B34040101010D01030102110101", "DNxHR LB"},
		{"060E2B34040101010D01030102110201", "DNxHR SQ"},
		{"060E2B34040101010D01030102110301", "DNxHR HQ"},
		{"060E2B34040101010D01030102110401", "DNxHR HQX"},
		{"060E2B34040101010D01030102110501", "DNxHR 444"},

		// DNxHR: Avid private
		{"060E2B34040101010E04020102110300", "DNxHR HQ"},
		{"060E2B34040101010E04020102110400", "DNxHR HQX"},

		// DNxHR: SMPTE
		{"060E2B340401010D0401020271250000", "DNxHR HQX"},
		{"060E2B340401010D0401020271260000", "DNxHR HQ"},
		{"060E2B340401010D0401020271270000", "DNxHR SQ"},
		{"060E2B340401010D0401020271280000", "DNxHR LB"},

		// DNxUncompressed (SMPTE RDD 44). Integer depths and 32-bit float
		// share 03070100 — bit depth lives in descriptor tag 0x3301, with
		// 254 meaning float — while 16-bit 2.14 fixed point has its own
		// UL. Verified against the July corpus.
		{"060E2B340401010D0401020203070100", "Avid DNxUncompressed"},
		{"060E2B340401010D0401020203070200", "Avid DNxUncompressed 2.14"},

		// Sony
		{"060E2B34040101010401020201020101", "Sony IMX 30"},
		{"060E2B34040101010401020201020102", "Sony IMX 40"},
		{"060E2B34040101010401020201020103", "Sony IMX 50"},
		{"060E2B34040101030401020201030300", "XDCAM EX 35"},
		{"060E2B34040101030401020201040200", "XDCAM EX 35"},
		{"060E2B34040101030401020201040300", "XDCAM HD 50"},

		// AVC
		{"060E2B340401010A0401020201323101", "AVC/XAVC Intra 100"},
		{"060E2B340401010A0401020201323102", "AVC/XAVC Intra 100"},
		{"060E2B340401010A0401020201323103", "AVC/XAVC Intra 100"},
		{"060E2B340401010A0401020201323108", "AVC/XAVC Intra 100"},
		{"060E2B340401010D0401020201323201", "XAVC HD Intra CBG Class 200"}, // 29.97 flavour
		{"060E2B340401010D0401020201323202", "XAVC HD Intra CBG Class 200"}, // MC 2025 flavour
		{"060E2B340401010D0401020201323203", "XAVC HD Intra CBG Class 200"},
		{"060E2B340401010D0401020201323204", "XAVC HD Intra CBG Class 200"},
		{"060E2B340401010A0401020201313001", "AVC-Intra 50"},
		{"060E2B340401010A0401020201323001", "AVC Intra 4:2:2"},
		{"060E2B340401010A0401020201323104", "AVC-Intra 100 (4:2:2)"},
		{"060E2B340401010A0401020201323109", "AVC-Intra 100"},
		{"060E2B340401010D0401020201312001", "AVC Long GOP"},
		{"060E2B340401010D0401020201314001", "AVC Long GOP"},
		{"060E2B340401010D0401020201316001", "AVC Long GOP"},

		// H.264 covers 800 Kbps and 1500 Kbps variants
		{"060E2B340401010D0401020201311101", "H.264 Proxy"},

		// DV
		{"060E2B34040101010401020202010200", "DV 25 420"},
		{"060E2B34040101010401020202020200", "DV 25 411"},
		{"060E2B34040101010401020202020400", "DV 50"},
		{"060E2B34040101010401020202020100", "DV 25 411"}, // NTSC flavour
		{"060E2B34040101010401020202020300", "DV 50"},	   // NTSC flavour
		{"060E2B34040101010401020202020500", "DVCPro HD"}, // 1080i59.94
		{"060E2B34040101010401020202020600", "DVCPro HD"}, // 1080i50

		// JPEG 2000
		{"060E2B34040101070401020203010100", "JPEG 2000"},
		{"060E2B340401010D040102020301020A", "JPEG 2000 IMF"},
		{"060E2B340401010D0401020203010312", "JPEG 2000 IMF"}, // YCrCb flavour, MC 2025 UHD transcode

		// Apple ProRes: Avid private
		{"060E2B34040101010D010301020C0101", "Apple ProRes Proxy"},
		{"060E2B34040101010D010301020C0201", "Apple ProRes LT"},
		{"060E2B34040101010D010301020C0301", "Apple ProRes 422"},
		{"060E2B34040101010D010301020C0401", "Apple ProRes HQ"},
		{"060E2B34040101010D010301020C0501", "Apple ProRes 4444"},

		// Apple ProRes: SMPTE
		{"060E2B340401010D0401020203060100", "Apple ProRes Proxy"},
		{"060E2B340401010D0401020203060200", "Apple ProRes LT"},
		{"060E2B340401010D0401020203060300", "Apple ProRes 422"},
		{"060E2B340401010D0401020203060400", "Apple ProRes HQ"},
		{"060E2B340401010D0401020203060500", "Apple ProRes 4444"},
		{"060E2B340401010D0401020203060600", "Apple ProRes 4444 XQ"},

		// Miscellaneous
		{"060E2B34040101010E04030101030200", "Avid Title/Matte"},
		{"4B464141000D4D4F", "Avid Title (Uncompressed)"},
		{"060E2B34040101010D01030102060100", kPcmAudioName},
		{"060E2B34040101010402020203020500", "MP2 Audio"},
	};
	*/

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
			else if ((b8 == 0x0E && b9 == 0x04) || b8 == 0x0D)
			{
				family = QStringLiteral("Avid");
			}
		}
		qCDebug(lcMxf) << "unrecognised essence label" << hexStr << "family:" << family;
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
		const char *technical, *brand, *r30, *r25, *r60, *rDefault;
	};
	static constexpr DnxEntry kDnxTiers[] = {
		// technical           brand           29.97/30  25      50/59.94  default
		{"DNxHD LB", "Avid DNx LB", "45", "36", "90", "36"},
		{"DNxHD SQ", "Avid DNx SQ", "145", "120", "115", "115"},
		{"DNxHD HQ", "Avid DNx HQ", "220", "185", "175", "175"},
		{"DNxHD HQX", "Avid DNx HQX", "220X", "185X", "175X", "175X"},
		{"DNxHD SQ (720p)", "Avid DNx SQ", "75", "60", "", "60"},
		{"DNxHD HQ (720p)", "Avid DNx HQ", "110", "90", "", "90"},
		{"DNxHD HQX (720p)", "Avid DNx HQX", "110x", "90x", "", "90x"},
		// DNxHR carries no per-rate bitrate names; the brand shows bare.
		// "HR" denoted high-resolution capability — the Resolution column
		// already communicates that, so the display doesn't repeat it.
		{"DNxHR LB", "Avid DNx LB", "", "", "", ""},
		{"DNxHR SQ", "Avid DNx SQ", "", "", "", ""},
		{"DNxHR HQ", "Avid DNx HQ", "", "", "", ""},
		{"DNxHR HQX", "Avid DNx HQX", "", "", "", ""},
		{"DNxHR 444", "Avid DNx 444", "", "", "", ""},
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
		else if (fps == QLatin1String("50") || fps == QLatin1String("59.94"))
			bitrate = e.r60;
		else
			bitrate = e.rDefault;
		if (bitrate[0] == '\0')
			return QLatin1String(e.brand);
		return QLatin1String(e.brand) + QLatin1String(" (DNxHD ") + QLatin1String(bitrate) +
			   QLatin1Char(')');
	}

	return baseName;
}