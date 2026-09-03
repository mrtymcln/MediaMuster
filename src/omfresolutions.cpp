// OMF-era (legacy Avid media, pre-MXF). The resolution-id → short-name
// table for OMF-era picture descriptors; see omfresolutions.h for where
// it sits. Entirely OMF-specific — no MXF-era row is ever looked up here.
//
// MARK: - Sources
//
// Every row was built from two things and nothing else:
//
//   1. The 80 OMF slates Media Composer 26.8.0.58987 ships in
//      /Applications/Avid Media Composer/SupportingFiles/Avid_MediaFiles/
//      (copied to tests/fixtures/omf/avid_supporting/). Their FILENAMES spell
//      out the resolution ("BLACK_720x243x2_JFIF35.omf",
//      "FORMAT_720x576x1_DV420.omf", "OFFLINE_720x486x1_MPEG50.omf" …) and
//      their descriptors carry the (DIDResolutionID, Compression) pair, so
//      each id was matched to a filename token by reading the bytes.
//   2. Avid's own resolution files in
//      /Applications/Avid Media Composer/SupportingFiles/Avid_Resolutions/
//      (*.vr), whose names end in Avid's 4-character resolution code
//      (JfifVideoRes_JF35.vr, DvVideoRes_D25B.vr, AvidUncompressed_Y422.vr,
//      MPEG2VideoRes_MP50.vr …) and whose first string is the short name
//      Media Composer displays. Each .vr ALSO stores the resolution id
//      itself: the big-endian u16 at file offset 0x84, four bytes past
//      the "AVRS"/"SRVA" marker, whose low byte is the DIDResolutionID
//      (the JFIF and AUNC files OR 0x4000 into the word: JF35 = 0x4052 →
//      82, Y422 = 0x4097 → 151; the DV and MPEG files write it bare:
//      D25A = 0x008C → 140, MP50 = 0x00A0 → 160). Every row below was
//      confirmed against that id field, not by token alone. The token →
//      code → short name chain is:
//
//        id   4CC   token     .vr      short name
//        78   JFIF  JFIF12S   J12S     15:1s
//        82   JFIF  JFIF35    JF35     20:1
//        104  JFIF  JFIF25P   J25P     28:1
//        110  JFIF  JFIF15m   J15m     10:1m
//        112  JFIF  JFIF20mP  J2mP     8:1m
//        140  DV/C  DV411     D25A     DV 25 411
//        141  DV/C  DV420     D25B     DV 25 420
//        142  DV/C  DV50      DV50     DV 50
//        143  DV/C  DV411P    D24P     DV25P 411   (the .vr spells it without a space)
//        144  DV/C  DV420P    D25P     DV 25P 420
//        151  AUNC  UNCOMP    Y422     1:1
//        152  AUNC  UNCOMP_24P 422P    1:1
//        160  MPG2  MPEG50    MP50     MPEG 50
//
//   UNVERIFIED — token only, no .vr exists for them in the bundle:
//        2500 DV/C  DV100_115 (1920x540x2, 25 fps)     DV 100 1080i
//        2502 DV/C  DV100_90  (1280x720x1, 59.94 fps)  DV 100 720p
//        2402 DV/C  the same DV100_90 files as MC 26.8's regenerated
//                   msmMMOB.mdb re-ids them (the .omf says 2502)  DV 100 720p
//
//   "MXF1" alias rows: the msmMMOB.mdb MC 26.8 regenerates beside the
//   slates writes the 4CC "MXF1" (with a SMPTE "uncompressed" label) where
//   the .omf files themselves say AUNC (151, 152) or DV/C (2500). Both
//   spellings map to the same name so the database and the file agree.
//   That 4CC is NOT Avid's name for these resolutions: Avid's own "MXF1"
//   resolution is ids 170/171, short name "MXF1:1" (11xVideoRes_1T1X.vr /
//   11XP.vr, id field 0xAA / 0xAB), which is why the alias rows are keyed
//   on the id and never on the 4CC alone.
//
// Ids 1235–1489 (the DNxHD family) are absent on purpose — see the header.
// Nothing here was copied from any external project; the facts above are
// Avid's own files read byte by byte.

#include "omfresolutions.h"

#include <QLatin1String>
#include <cstring>

namespace
{
	struct Row
	{
		quint32 id;
		const char *fourcc; ///< Exactly four characters.
		const char *name;
	};

	constexpr Row kRows[] = {
		// MARK: JFIF (Avid's Motion-JPEG family)
		{78, "JFIF", "15:1s"},
		{82, "JFIF", "20:1"},
		{104, "JFIF", "28:1"},
		{110, "JFIF", "10:1m"},
		{112, "JFIF", "8:1m"},
		// MARK: DV 25 / DV 50
		{140, "DV/C", "DV 25 411"},
		{141, "DV/C", "DV 25 420"},
		{142, "DV/C", "DV 50"},
		{143, "DV/C", "DV25P 411"},
		{144, "DV/C", "DV 25P 420"},
		// MARK: Uncompressed (AUNC in the file, MXF1 in MC 26.8's database)
		{151, "AUNC", "1:1"},
		{151, "MXF1", "1:1"},
		{152, "AUNC", "1:1"},
		{152, "MXF1", "1:1"},
		// MARK: MPEG-2
		{160, "MPG2", "MPEG 50"},
		// MARK: DV 100 — UNVERIFIED (filename tokens only; no .vr in the bundle)
		{2500, "DV/C", "DV 100 1080i"},
		{2500, "MXF1", "DV 100 1080i"},
		{2502, "DV/C", "DV 100 720p"},
		{2402, "DV/C", "DV 100 720p"},
	};
} // namespace

QString OmfResolutions::name(quint32 resolutionId, const QByteArray &compression)
{
	// The property is a NUL-terminated string ("JFIF\0"); compare the
	// four characters before the terminator and nothing else.
	const int len = int(qstrnlen(compression.constData(), compression.size()));
	if (len != 4)
		return {};
	for (const Row &r : kRows)
	{
		if (r.id == resolutionId && std::memcmp(r.fourcc, compression.constData(), 4) == 0)
			return QLatin1String(r.name);
	}
	return {};
}

// MARK: - Audio

// Source: Media Composer 26.8.0.58987's own format-menu strings, read from
// its binary on 2026-09-02 — "AIFF-C  (OMF)", "WAVE  (OMF)", "SDII", beside
// "PCM  (Avid OP-Atom)" and "PCM  (MXF OP1a)" for the MXF era (the double
// space is Media Composer's menu alignment and is collapsed here). The bins
// it wrote the fixture audio into are named the same way: "WAVE(OMF)" and
// "AIFF-C(OMF)". SDII carries no wrapper tag in Avid's own list, so none is
// added; no SDII specimen exists (see the SD2D note in omfobjects.cpp).
QString OmfResolutions::audioName(const QByteArray &descriptorClass)
{
	if (descriptorClass == "WAVD")
		return QStringLiteral("WAVE (OMF)");
	if (descriptorClass == "AIFD")
		return QStringLiteral("AIFF-C (OMF)");
	if (descriptorClass == "SD2D")
		return QStringLiteral("SDII");
	return {};
}
