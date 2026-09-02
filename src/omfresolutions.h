#pragma once

// OMF-era (legacy Avid media, pre-MXF). An OMF essence file is an Apple
// Bento container with the essence first and the object table of contents
// at the tail; it lives flat in "OMFI MediaFiles" beside a version-2
// msmFMID.pmr (8-byte MOBs) and a msmMMOB.mdb whose mobs carry 12-byte
// omfi:UIDs. Its picture descriptor names the codec not with a SMPTE label
// but with a four-character family code (OMFI:DIDD:Compression: "JFIF",
// "DV/C", "AUNC", "MPG2", "AVHD") plus Avid's numeric resolution id
// (OMFI:DIDD:DIDResolutionID). This module is the compiled-in table that
// turns that pair into the bare short name Avid itself shows ("20:1",
// "DV 25 420", "1:1", "MPEG 50"). MXF-era handling (MxfParser's label
// table, the DNx tiers) lives elsewhere and is unaffected: the DNxHD-era
// ids 1235–1489 are deliberately absent here so they keep routing through
// OmfObjects::ulFromResId and the app's existing DNx names.

#include <QByteArray>
#include <QString>

namespace OmfResolutions
{
	/// The bare Avid short name for (resolution id, compression 4CC), or
	/// empty when the table has no row for that pair. `compression` is the
	/// raw OMFI:DIDD:Compression bytes — a NUL-terminated string, so
	/// "JFIF\0" and "JFIF" both match. The 4CC is part of the key on
	/// purpose: a reused id under another family must never mislabel.
	[[nodiscard]] QString name(quint32 resolutionId, const QByteArray &compression);
} // namespace OmfResolutions
