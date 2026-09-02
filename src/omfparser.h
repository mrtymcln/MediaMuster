#pragma once

// OMF-era (legacy Avid media, pre-MXF). An OMF essence file is an Apple
// Bento container with the essence first and the object table of contents
// at the tail; it lives flat in "OMFI MediaFiles" beside a version-2
// msmFMID.pmr (8-byte MOBs) and a msmMMOB.mdb whose mobs carry 12-byte
// omfi:UIDs instead of the 32-byte UMIDs every MXF-era source writes.
// This is the reader for ONE such file: the OMF-era twin of
// MxfParser::parseHeader, producing the same MxfMetadata so the table
// cannot tell which era a row came from. MXF header handling (MxfParser)
// lives elsewhere and is unaffected; the object walks are shared with the
// MDB reader through OmfObjects.

#include "mxfparser.h"

#include <QString>

// MARK: - OmfMetadata

/// What one OMF essence file says about itself. `essence` is filled the
/// way MxfParser fills it from a header and run through the same
/// MxfParser::finalise, so codec / resolution / fps / duration / bit
/// depth / audio facts are derived by one piece of code for both eras.
/// The extra fields are the facts an OMF file carries that a header does
/// not surface: the master's bin (the MDB is the only MXF-era source of
/// it), the file's own locator, and the start timecode (read because it
/// is free here; held back from MediaFile until a column wants it).
struct OmfMetadata
{
	/// `umid` = the MASTER mob's canonical hex (the wrapped 32-byte form,
	/// equal to the v2 PMR's masterMobId); `clipName` = the master's
	/// OMFI:CPNT:Name with `clipNameFromMaterial` set; `projectName` = the
	/// `_PJ` attribute searched master → file → source mob (the 2021 slates
	/// keep it on the source mob, MC 2026 on the file mob); `isPrecompute`
	/// = master UsageCode 1; `dropFrame` = TCCP Flags != 0.
	MxfMetadata essence;

	/// The media-data object's MobID in canonical hex — the FILE mob, equal
	/// to the v2 PMR's mobId. Never equal to `essence.umid`.
	QString fileMobId;

	/// `_ORG_BIN` → MCBR → OMFI:MCBR:MC:binName on the master mob.
	QString bin;

	/// `_MEDIAFILE` locator on the file mob — where the writer thought the
	/// file lived. Diagnostic only; often empty.
	QString mediaFilePath;

	/// OMFI:TCCP:StartTC in frames (−1 when no timecode component is
	/// reachable) and OMFI:TCCP:FPS. Not surfaced in MediaFile.
	qint64 startTimecode = -1;
	int timecodeFps = 0;
};

// MARK: - OmfParser

/// Reads an OMF essence file the cheap way — BentoFile::open reads the
/// label, the TOC and the property dictionary (a few KB regardless of file
/// size) and every value after that by seek+read — then follows the three
/// mob records (master, file, source) the way MdbParser does for a
/// database row. A plain RIFF `.wav` / `.aif` that Avid did not write has
/// no Bento label and fails at the first 24-byte read (`valid=false`,
/// `bytesRead == 24`); the scanner treats that as "no OMF metadata", never
/// as an error.
class OmfParser
{
public:
	/// Parse the file's tail. `essence.valid` is false when the file is not
	/// a Bento container, carries no mobs, or has no media descriptor the
	/// walker recognises; whatever was read stays in the struct. `bytesRead`
	/// (optional) receives BentoFile::bytesRead() for the scanner's summary
	/// log line, exactly as MxfParser::parseHeader reports it.
	[[nodiscard]] static OmfMetadata parseHeader(const QString &filePath, qint64 *bytesRead = nullptr);
};
