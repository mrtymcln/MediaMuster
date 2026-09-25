#pragma once

// Maps a legacy Avid picture descriptor's compression 4CC and numeric
// resolution id to its short codec name, for OMF files and their MDBs.
// DNxHD ids 1235–1489 are deliberately absent: OmfObjects::ulFromResId
// resolves those through the shared codec-label path.

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

	/// Avid's own label for an OMF-era AUDIO descriptor class, or empty for
	/// anything else. Media Composer names its legacy audio by container,
	/// not by coding: its supported OMF formats are "WAVE (OMF)" and
	/// "AIFF-C (OMF)". The MXF-era label is "PCM (Avid OP-Atom)", which is why
	/// MXF-era audio keeps finalise's "PCM" and never comes through here.
	/// `descriptorClass` is the OMFI:ObjID FourCC ("WAVD", "AIFD").
	[[nodiscard]] QString audioName(const QByteArray &descriptorClass);
} // namespace OmfResolutions
