#pragma once

#include <QByteArray>
#include <QString>

// MARK: - MxfMetadata

/// Technical metadata pulled from one MXF file's header partition.
/// `valid=false` means the parser didn't find enough information to
/// classify the file — usually a corrupt or non-Avid MXF. Empty
/// strings mean "field absent in this MXF", not "field is empty".
struct MxfMetadata
{
	QString codec;                    ///< Resolved codec name, e.g. "DNxHD HQ (DNxHD 220)".
	QString resolution;               ///< "1920x1080", or empty for audio.
	QString fps;                      ///< "23.976", "25", "29.97", etc.
	QString bitDepth;                 ///< "8-bit", "10-bit", "24-bit".
	QString umid;                     ///< Canonical hex UMID from tag 0x4401 in the material package.
	QString clipName;                 ///< Clip name from tag 0x4402 in the material package.
	QByteArray essenceContainerLabel; ///< Raw 16-byte UL — looked up against kEntries.
	int width = 0;
	int height = 0;     ///< Stored value — interlaced files store one field height.
	int channels = 0;   ///< Audio only.
	int sampleRate = 0; ///< Audio Hz.

	/// 0 = Full Frame, 1 = Separate Fields, 2 = Single Field, 3 = Mixed Fields.
	/// Used to decide whether to double `height` and to pick `i` vs `p`
	/// for DV codec name formatting.
	int frameLayout = -1;

	qint64 durationFrames = 0;
	bool isAudio = false;
	bool valid = false;
};

// MARK: - MxfParser

/// Reads an MXF file's header partition and pulls out the metadata
/// the table needs (codec, resolution, fps, duration, UMID, clip
/// name). Only the first 256–512 KB is read — essence data is never
/// touched. See `mxfparser.cpp` for the KLV walk and the codec lookup.
class MxfParser
{
public:
	/// Parse the MXF file's header. On success, `valid=true` and the
	/// metadata fields are populated. On failure (open error, no
	/// header partition pack found, no recognisable descriptors),
	/// returns a default-constructed MxfMetadata with `valid=false`.
	///
	/// `bytesRead`, if non-null, receives the number of bytes the
	/// parser actually read from disk — useful for telemetry in the
	/// scanner's "MXF parse" summary log line.
	[[nodiscard]] static MxfMetadata parseHeader(const QString &filePath,
	                                             qint64 *bytesRead = nullptr);

	/// Map a 16-byte essence-container UL to the marketing name like
	/// "DNxHD HQ (DNxHD 220)". `fps` is needed to resolve the
	/// DNxHD bitrate (the UL identifies the tier but not the rate).
	/// Returns "Unknown (HEX)" for ULs not in our table.
	[[nodiscard]] static QString codecFromEssenceLabel(const QByteArray &label, const QString &fps);

private:
	[[nodiscard]] static MxfMetadata parseFromBuffer(const QByteArray &data);
	[[nodiscard]] static qint64 readBerLength(const QByteArray &data, qint64 offset, int &bytesUsed);
	[[nodiscard]] static quint16 readUint16BE(const QByteArray &data, qint64 offset);
	[[nodiscard]] static quint32 readUint32BE(const QByteArray &data, qint64 offset);

	static void parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
	static void parseMaterialPackage(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
	static void parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out);
};