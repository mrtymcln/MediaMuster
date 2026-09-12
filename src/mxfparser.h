#pragma once

#include "mediametadata.h"

class QFile;

// MARK: - MxfParser

/// Reads an MXF file's header partition and pulls out the metadata
/// the table needs (codec, resolution, fps, duration, UMID, clip
/// name). Walks complete metadata KLVs, skipping padding and stopping at
/// essence or the next partition. Metadata allocation is bounded; reaching
/// that bound is reported explicitly rather than treated as success.
class MxfParser
{
public:
	/// Parse the MXF file's header. On success, `valid=true` and the
	/// metadata fields are populated. A file it can't open returns a
	/// MediaMetadata with IoError status; a file it opens but can't classify
	/// (no recognisable descriptors) returns whatever fields it did read,
	/// with `valid=false`. Standalone metadata KLVs at offset 0 retain a
	/// recovery path when the header-partition pack is absent.
	///
	/// `bytesRead`, if non-null, receives the number of bytes the
	/// parser actually read from disk; useful for telemetry in the
	/// scanner's "MXF parse" summary log line.
	[[nodiscard]] static MediaMetadata parseHeader(const QString &filePath,
												 qint64 *bytesRead = nullptr);

	/// Read through a caller-owned handle without reopening or closing it.
	/// Starts at offset zero and changes the cursor; the caller retains ownership
	/// and any native sharing protection. Requires a readable, seekable file.
	[[nodiscard]] static MediaMetadata parseHeader(QFile &file, qint64 *bytesRead = nullptr);

private:
	[[nodiscard]] static MediaMetadata parseFromBuffer(const QByteArray &data);
	[[nodiscard]] static qint64 readBerLength(const QByteArray &data, qint64 offset,
											  int &bytesUsed);
	[[nodiscard]] static quint16 readUint16BE(const QByteArray &data, qint64 offset);
	[[nodiscard]] static quint32 readUint32BE(const QByteArray &data, qint64 offset);

	static void parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length,
								   MediaMetadata &out);
	/// Handles both MaterialPackage (0x36) and SourcePackage (0x37) sets. The
	/// MaterialPackage is authoritative for clip name and UMID; a SourcePackage
	/// only supplies them as a fallback when still unset. The flag lets the
	/// material values win regardless of the packages' byte order in the header.
	static void parsePackage(const QByteArray &data, qint64 startPos, qint64 length,
							 MediaMetadata &out, bool isMaterialPackage);
	static void parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length,
										 MediaMetadata &out);
	/// AAF TaggedValue set (0x3F): Name (0x5001, UTF-16BE) + Value (0x5003,
	/// an Indirect: type AUID + payload). Only four names are read —
	/// `UNC Path`, `Video`, `_IMPORTSETTING`, `_PJ`/`PROJNAME` — see
	/// MediaMetadata::sourceFilePath.
	static void parseTaggedValue(const QByteArray &data, qint64 startPos, qint64 length,
								 MediaMetadata &out);
};
