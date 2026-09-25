#pragma once

// Reads OMF1/OMF2 metadata from Bento containers, including supported
// embedded omfi chunks in RIFF/RF64 WAVE files. Produces one essence row
// using the same MediaMetadata as MxfParser. Object walks are shared
// with the MDB reader through OmfObjects.

#include "mediametadata.h"
#include "omfobjects.h"

#include <QString>

// MARK: - OmfMetadata

/// What one OMF essence file says about itself. `essence` is filled the
/// way MxfParser fills it from a header and run through the same
/// MediaMetadataUtil::finalise, so codec / resolution / fps / duration / bit
/// depth / audio facts are derived by one piece of code for both eras.
/// Extra fields hold original-bin metadata, the file's recorded locator
/// and start timecode. Original-bin metadata can also come from MDBs or AVBs;
/// the MXF header reader does not expose it.
struct OmfMetadata
{
	OmfObjects::Revision revision = OmfObjects::Revision::Unknown;
	/// A unique file mob owns a recognized OMF essence descriptor. This
	/// establishes the container even when technical fields are incomplete.
	bool hasMediaDescriptor = false;
	/// `umid` and `clipName` come from the linked master mob. IDs use
	/// OmfUid::canonicalHex, including the Avid wrapper and general OMF
	/// namespace. `_PJ` is searched master → file → source mob; precompute
	/// classification comes from the master's usage code.
	MediaMetadata essence;

	/// Canonical file-mob ID, selected by embedded media identity or a
	/// unique media descriptor. Avid prefix-42 IDs use the legacy PMR wrapper.
	QString fileMobId;

	/// `_ORG_BIN` → MCBR → OMFI:MCBR:MC:binNameUTF8, else MC:binName.
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

/// BentoFile::open reads the label, TOC and property dictionary, then
/// fetches requested metadata values on demand. Work scales with the
/// metadata and mob graph, not the essence payload. Both OMF1 and OMF2
/// schemas are read. A plain audio file
/// without a Bento tail or supported embedded omfi chunk yields no OMF
/// metadata. A file containing several independent media objects does not
/// have one unambiguous essence row and is not collapsed into the first one.
class OmfParser
{
public:
	/// Parse supported OMF metadata. `essence.valid` is false when the file is not
	/// a Bento container, carries no mobs, or has no media descriptor the
	/// walker recognises; whatever was read stays in the struct. `bytesRead`,
	/// if non-null, receives BentoFile::bytesRead().
	[[nodiscard]] static OmfMetadata parseHeader(const QString &filePath, qint64 *bytesRead = nullptr);
};
