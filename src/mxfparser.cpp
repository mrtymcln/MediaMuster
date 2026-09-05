#include "mxfparser.h"
#include "avidusage.h"
#include "logcategories.h"
#include "mobid.h"
#include "mxfproperties.h"
#include <QByteArrayView>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <limits>

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

static bool isMetadataSetKey(const char *key)
{
	return std::memcmp(key, kUlSetPrefix, 7) == 0 &&
		std::memcmp(key + 8, kUlSetPrefix + 8, 5) == 0;
}

static bool isUsefulMetadataSet(quint8 type)
{
	switch (type)
	{
	case 0x0f: case 0x11: case 0x14: case 0x18: case 0x23: case 0x27:
	case 0x28: case 0x29: case 0x2f: case 0x32: case 0x36: case 0x37:
	case 0x39: case 0x3a: case 0x3b: case 0x3f: case 0x42: case 0x44:
	case 0x47: case 0x48: case 0x51: case 0x5e:
		return true;
	default:
		return false;
	}
}

// MARK: - UsageCode (Media vs Precompute)
//
// The private Avid integer and the standard AAF/MXF UsageCode UID are
// independent properties. MC26.8 AddAttributesToAAFMob (arm64 0x1882f8–
// 0x1884b0) maps integer1/4/6 to the SAME LowerLevel UID, so that UID alone
// does not establish a precompute. AvidUsage centralizes the supported
// master1/master7 verdicts and rejects conflicting or unknown positive codes.
// The private property is resolved through the Primer, never a guessed tag.
// All171 rendered MXFs in the 2,493-row export have private1; all2,240 ordinary
// MXFs omit both properties. An identified, successfully read material package
// with neither property retains that ordinary-media convention.

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
	if (len < 4 || len > 8)
		return -1;
	const int take = qMin<int>(len, 8);
	const auto *p = reinterpret_cast<const uchar *>(data.constData() + pos + len - take);
	quint64 v = 0;
	for (int i = 0; i < take; ++i)
		v = (v << 8) | p[i];
	return v <= quint64(std::numeric_limits<qint64>::max()) ? qint64(v) : -1;
}

/// Read a BER-encoded length. MXF uses BER short form (one byte,
/// high bit clear) for lengths 0–127 and BER long form (one count
/// byte plus N value bytes) for longer lengths. Sets `bytesUsed` to
/// the total number of bytes consumed; returns -1 on a malformed
/// length.
qint64 MxfParser::readBerLength(const QByteArray &data, qint64 offset, int &bytesUsed)
{
	if (offset < 0 || offset >= data.size())
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
	quint64 length = 0;
	for (int i = 0; i < lenBytes; ++i)
		length = (length << 8) | static_cast<quint8>(data[offset + 1 + i]);
	if (length > quint64(std::numeric_limits<qint64>::max()))
	{
		bytesUsed = 0;
		return -1;
	}
	return qint64(length);
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

// Avid's GetHeaderFromFile feeds its parser incrementally; header allocation
// (256/512 KiB in many specimens) is not a format limit. Walk KLV framing and
// skip padding/unknown payloads without loading essence into memory.
MxfMetadata MxfParser::parseHeader(const QString &filePath, qint64 *bytesRead)
{
	using Status = MxfMetadata::HeaderStatus;
	qint64 readCount = 0;
	if (bytesRead)
		*bytesRead = 0;
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		MxfMetadata result;
		result.headerStatus = Status::IoError;
		qCWarning(lcMxf) << "cannot open" << filePath << file.errorString();
		return result;
	}
	auto read = [&](qint64 count) {
		QByteArray result = file.read(count);
		readCount += result.size();
		return result;
	};
	constexpr qint64 kRunInLimit = 64 * 1024;
	constexpr qint64 kMetadataLimit = 64 * 1024 * 1024;
	const QByteArray partitionPrefix = QByteArray::fromRawData(kUlHeaderPartition, 14);
	const QByteArray primerKey = QByteArray::fromHex("060e2b34020501010d01020101050100");
	QByteArray search;
	qsizetype partition = -1;
	while (search.size() < kRunInLimit + 16 && !file.atEnd())
	{
		const QByteArray next = read(qMin<qint64>(8192, kRunInLimit + 16 - search.size()));
		if (next.isEmpty())
			break;
		search += next;
		partition = search.indexOf(partitionPrefix);
		if (partition >= 0)
			break;
		// Retain the established recovery path for standalone metadata KLVs.
		if (search.size() >= 16 && isMetadataSetKey(search.constData()))
			break;
	}
	Status status = Status::Complete;
	if (partition > kRunInLimit || (partition < 0 &&
		(search.size() < 16 || !isMetadataSetKey(search.constData()))))
		status = Status::Malformed;
	if (!file.seek(partition >= 0 ? partition : 0))
		status = Status::IoError;
	QByteArray metadata;
	QByteArray partitionEssenceContainer;
	bool sawPartition = false;
	quint64 declaredHeaderBytes = 0;
	qint64 metadataEnd = -1;
	bool shortFinalFill = false;
	int items = 0;
	while (status == Status::Complete && !file.atEnd())
	{
		if (metadataEnd >= 0 && file.pos() >= metadataEnd)
			break;
		const qint64 keyStart = file.pos();
		if (++items > 1000000)
		{
			status = Status::LimitExceeded;
			break;
		}
		const QByteArray key = read(16);
		if (key.size() != 16)
		{
			status = Status::Incomplete;
			break;
		}
		const bool isSet = isMetadataSetKey(key.constData()) && isUsefulMetadataSet(quint8(key[14]));
		const bool isPartition = key.startsWith(QByteArray::fromHex("060e2b34020501010d0102010102")) ||
			key.startsWith(QByteArray::fromHex("060e2b34020501010d0102010103")) ||
			key.startsWith(QByteArray::fromHex("060e2b34020501010d0102010104"));
		// GC essence elements and subsequent body/footer partitions end this
		// header. Their payload may be gigabytes; do not read or allocate it.
		const bool isEssence = key.startsWith(QByteArray::fromHex("060e2b34010201010d010301"));
		if (isEssence || (isPartition && sawPartition))
		{
			if (metadataEnd >= 0 && keyStart < metadataEnd)
				status = Status::Malformed;
			break;
		}
		if (isPartition)
			sawPartition = true;
		QByteArray ber = read(1);
		if (ber.size() != 1)
		{
			status = Status::Incomplete;
			break;
		}
		const quint8 first = quint8(ber[0]);
		if (first >= 0x80)
		{
			const int width = first & 0x7f;
			if (width == 0 || width > 8)
			{
				status = Status::Malformed;
				break;
			}
			ber += read(width);
			if (ber.size() != width + 1)
			{
				status = Status::Incomplete;
				break;
			}
		}
		int used = 0;
		const qint64 length = readBerLength(ber, 0, used);
		if (length < 0)
		{
			status = Status::Malformed;
			break;
		}
		if (metadataEnd < 0 && declaredHeaderBytes > 0 && (isSet || key == primerKey))
		{
			if (declaredHeaderBytes > quint64(std::numeric_limits<qint64>::max() - keyStart))
			{
				status = Status::Malformed;
				break;
			}
			metadataEnd = keyStart + qint64(declaredHeaderBytes);
		}
		if (metadataEnd >= 0 && (file.pos() > metadataEnd || length > metadataEnd - file.pos()))
		{
			status = Status::Malformed;
			break;
		}
		if (length > file.size() - file.pos())
		{
			// Captured headers may end inside KLV Fill. Padding carries no
			// metadata, so an otherwise complete description is still usable.
			// This status certifies metadata, never the integrity of essence.
			if (key.left(7) == QByteArray::fromHex("060e2b34010101") &&
				key.mid(8) == QByteArray::fromHex("0301021001000000") &&
				metadataEnd >= file.pos() && length == metadataEnd - file.pos())
			{
				shortFinalFill = true;
				break;
			}
			status = Status::Incomplete;
			break;
		}
		if (isPartition)
		{
			if (length < 88)
			{
				status = Status::Malformed;
				break;
			}
			const QByteArray fixed = read(88);
			if (fixed.size() != 88)
			{
				status = Status::Incomplete;
				break;
			}
			declaredHeaderBytes = qFromBigEndian<quint64>(fixed.constData() + 32);
			const quint32 count = qFromBigEndian<quint32>(fixed.constData() + 80);
			const quint32 stride = qFromBigEndian<quint32>(fixed.constData() + 84);
			// Empty legacy packs can use stride zero. Non-empty UL batches
			// have exactly 16 bytes per entry, bounded by the enclosing KLV.
			if ((count == 0 && stride != 0 && stride != 16) || (count > 0 && stride != 16) ||
				quint64(count) * 16 != quint64(length - 88))
			{
				status = Status::Malformed;
				break;
			}
			if (count == 1)
			{
				partitionEssenceContainer = read(16);
				if (partitionEssenceContainer.size() != 16) status = Status::Incomplete;
			}
			else if (!file.seek(file.pos() + length - 88))
				status = Status::IoError;
		}
		else if (isSet || key == primerKey)
		{
			if (length > kMetadataLimit - metadata.size() - key.size() - ber.size())
			{
				status = Status::LimitExceeded;
				break;
			}
			const QByteArray value = read(length);
			if (value.size() != length)
			{
				status = file.error() == QFileDevice::NoError ? Status::Incomplete : Status::IoError;
				break;
			}
			metadata += key;
			metadata += ber;
			metadata += value;
		}
		else if (!file.seek(file.pos() + length))
			status = Status::IoError;
	}
	if (status == Status::Complete && metadataEnd > file.size() && !shortFinalFill)
		status = Status::Incomplete;
	MxfMetadata result = parseFromBuffer(metadata);
	if (result.headerStatus == Status::Malformed)
		status = Status::Malformed;
	result.headerStatus = status;
	// Avid's alpha-only MXF files omit PictureEssenceCoding. Their single
	// partition container positively identifies uncompressed RGBA; the
	// selected descriptor's A:8 layout establishes the alpha component.
	if (status == Status::Complete && result.valid && result.codec.isEmpty() &&
		!result.pictureCodingPresent && result.rgbaDescriptor && result.rgbaAlpha8 &&
		partitionEssenceContainer == QByteArray::fromHex(kAvidUncRgbaContainerHex))
	{
		result.codec = QStringLiteral("Uncompressed alpha");
		result.bitDepth = QStringLiteral("8-bit");
	}
	if (status != Status::Complete)
	{
		result.valid = false;
		result.hasMaterialPackage = false;
		result.classificationKnown = false;
		result.precomputeCategory = AvidPrecompute::Category::Unknown;
	}
	if (!result.valid)
		qCWarning(lcMxf) << "no complete usable MXF metadata in" << filePath
			<< "status" << int(status) << "read" << readCount << "bytes";
	if (bytesRead)
		*bytesRead = readCount;
	return result;
}

// MARK: - KLV walk

MxfMetadata MxfParser::parseFromBuffer(const QByteArray &data)
{
	struct Set
	{
		quint8 type = 0;
		QByteArray local;
		QHash<quint16, QByteArray> fields;
		QSet<quint16> identifiedProperties; // Primer-confirmed private property identity.
	};
	MxfMetadata meta;
	QVector<Set> sets;
	QHash<quint16, quint16> primer;
	QVector<QPair<quint8, QByteArray>> rawSets;
	const QByteArray primerKey = QByteArray::fromHex("060e2b34020501010d01020101050100");
	static const QHash<QByteArray, quint16> properties = [] {
		QHash<QByteArray, quint16> result;
		for (const auto &entry : kMxfProperties)
		{
			QByteArray key = QByteArray::fromHex(entry.hex);
			if (key.startsWith(QByteArray::fromHex("060e2b34")))
				key[7] = 1; // registry version does not change property identity
			result.insert(key, entry.tag);
		}
		return result;
	}();
	auto fail = [&] {
		MxfMetadata result;
		result.headerStatus = MxfMetadata::HeaderStatus::Malformed;
		return result;
	};
	for (qint64 pos = 0; pos < data.size();)
	{
		if (data.size() - pos < 17)
			return fail();
		int used = 0;
		const qint64 size = readBerLength(data, pos + 16, used);
		const qint64 value = pos + 16 + used;
		if (size < 0 || value > data.size() || size > data.size() - value)
			return fail();
		if (data.mid(pos, 16) == primerKey)
		{
			if (size < 8)
				return fail();
			const quint32 count = readUint32BE(data, value);
			const quint32 stride = readUint32BE(data, value + 4);
			if (stride != 18 || count > quint64(size - 8) / stride || quint64(count) * stride != quint64(size - 8))
				return fail();
			for (quint32 i = 0; i < count; ++i)
			{
				const qint64 entry = value + 8 + qint64(i) * stride;
				const quint16 tag = readUint16BE(data, entry);
				QByteArray key = data.mid(entry + 2, 16);
				if (key.startsWith(QByteArray::fromHex("060e2b34")))
					key[7] = 1;
				const quint16 canonical = properties.value(key, 0);
				if (primer.contains(tag) && primer.value(tag) != canonical)
					return fail();
				primer.insert(tag, canonical);
			}
		}
		else if (isMetadataSetKey(data.constData() + pos) && isUsefulMetadataSet(quint8(data[pos + 14])))
			rawSets.append({quint8(data[pos + 14]), data.mid(value, size)});
		pos = value + size;
	}
	QHash<QByteArray, int> byInstance;
	QHash<QByteArray, int> byPackage;
	QVector<int> materials, files, descriptors;
	auto isDescriptor = [](quint8 type) {
		return type == kSetCdci || type == kSetRgba || type == kSetWave ||
			type == kSetAes3 || type == kSetSoundMpeg || type == 0x27 || type == 0x42 || type == 0x51;
	};
	for (const auto &raw : rawSets)
	{
		Set set;
		set.type = raw.first;
		for (qint64 p = 0; p < raw.second.size();)
		{
			if (raw.second.size() - p < 4)
				return fail();
			const quint16 localTag = readUint16BE(raw.second, p);
			const quint16 size = readUint16BE(raw.second, p + 2);
			p += 4;
			if (size > raw.second.size() - p)
				return fail();
			const quint16 tag = localTag == AvidUsage::kPrivateMxfTag && !primer.contains(localTag) ?
				0 : primer.value(localTag, localTag);
			if (tag != 0)
			{
				const QByteArray value = raw.second.mid(p, size);
				if ((tag == 0x4408 && size != 16) || (tag == AvidUsage::kPrivateMxfTag && size != 4) ||
					(tag == 0x4401 && size != 32) ||
					(tag == 0x3c0a && size != 16) || (tag == 0x4701 && size != 16) ||
					(tag == 0x4803 && size != 16) || (tag == 0x4b01 && size != 8))
					return fail();
				if (set.fields.contains(tag) && set.fields.value(tag) != value)
					return fail();
				set.fields.insert(tag, value);
				if (primer.contains(localTag)) set.identifiedProperties.insert(tag);
				set.local += char(tag >> 8);
				set.local += char(tag & 0xff);
				set.local += char(size >> 8);
				set.local += char(size & 0xff);
				set.local += value;
			}
			p += size;
		}
		const int index = sets.size();
		const QByteArray instance = set.fields.value(0x3c0a);
		if (instance.size() == 16)
		{
			if (byInstance.contains(instance))
				return fail();
			byInstance.insert(instance, index);
		}
		if (set.type == kSetMatPkg)
			materials.append(index);
		if (set.type == kSetSrcPkg && set.fields.contains(0x4701))
			files.append(index);
		if (set.type == kSetMatPkg || set.type == kSetSrcPkg)
			byPackage.insert(set.fields.value(0x4401), index);
		if (isDescriptor(set.type))
			descriptors.append(index);
		sets.append(std::move(set));
	}
	// Decode references only when their shape is valid. Bounded traversal also
	// handles cycles and shared objects without recursive stack growth.
	auto refs = [&](const QByteArray &value) {
		QVector<int> result;
		if (value.size() == 16)
		{
			const auto found = byInstance.constFind(value);
			if (found != byInstance.constEnd()) result.append(found.value());
		}
		else if (value.size() >= 8)
		{
			const quint32 count = readUint32BE(value, 0), stride = readUint32BE(value, 4);
			if (stride == 16 && count == quint64(value.size() - 8) / 16 && (value.size() - 8) % 16 == 0)
				for (quint32 n = 0; n < count; ++n)
				{
					const auto found = byInstance.constFind(value.mid(8 + qsizetype(n) * 16, 16));
					if (found != byInstance.constEnd()) result.append(found.value());
				}
		}
		return result;
	};
	auto descendants = [&](int root, bool followSource) {
		QSet<int> visited;
		QVector<int> queue{root};
		for (qsizetype n = 0; n < queue.size(); ++n)
		{
			const int index = queue[n];
			if (index < 0 || visited.contains(index)) continue;
			visited.insert(index);
			for (const auto &value : sets[index].fields)
				for (int target : refs(value)) if (!visited.contains(target)) queue.append(target);
			if (followSource && sets[index].type == kSetSourceClip)
			{
				const auto target = byPackage.constFind(sets[index].fields.value(0x1101));
				if (target != byPackage.constEnd() && !visited.contains(target.value())) queue.append(target.value());
			}
		}
		return visited;
	};
	int filePackage = -1;
	// EssenceContainerData explicitly identifies the package for the stored
	// essence. Prefer it over an unrelated/tape SourcePackage in the header.
	QSet<int> linkedFiles;
	for (const auto &set : sets)
		if (set.type == 0x23)
		{
			const int candidate = byPackage.value(set.fields.value(0x2701), -1);
			if (files.contains(candidate)) linkedFiles.insert(candidate);
		}
	if (linkedFiles.size() == 1) filePackage = *linkedFiles.constBegin();
	else if (linkedFiles.isEmpty() && files.size() == 1) filePackage = files.first();
	int material = -1;
	if (filePackage >= 0)
	{
		const QByteArray id = sets[filePackage].fields.value(0x4401);
		if (id.size() == MobId::kRawSize)
			meta.fileMobId = MobId::format(reinterpret_cast<const unsigned char *>(id.constData()));
		for (int candidate : materials)
			if (descendants(candidate, true).contains(filePackage))
			{
				if (material >= 0) { material = -2; break; }
				material = candidate;
			}
	}
	if (material == -1 && materials.size() == 1) material = materials.first();
	// An explicit Preface primary-package reference resolves otherwise
	// ambiguous connected material packages.
	QSet<int> primaryMaterials;
	for (const auto &set : sets)
		if (set.type == 0x2f)
			for (int candidate : refs(set.fields.value(0x3b08)))
				if (materials.contains(candidate) && (filePackage < 0 || descendants(candidate, true).contains(filePackage)))
					primaryMaterials.insert(candidate);
	if (primaryMaterials.size() == 1) material = *primaryMaterials.constBegin();
	if (material >= 0)
	{
		const auto &set = sets[material];
		parsePackage(set.local, 0, set.local.size(), meta, true);
		const auto privateUsage = set.fields.constFind(AvidUsage::kPrivateMxfTag);
		const qint32 code = privateUsage == set.fields.cend() ? AvidUsage::kMissing :
			AvidUsage::integerCode(readUint32BE(privateUsage.value(), 0));
		const auto classification = AvidUsage::materialClassification(
			code, AvidUsage::standardUsage(set.fields.value(0x4408)));
		meta.hasMaterialPackage = set.fields.value(0x4401).size() == MobId::kRawSize;
		meta.classificationKnown = meta.hasMaterialPackage &&
			classification != AvidUsage::Classification::Unknown;
		meta.isPrecompute = classification == AvidUsage::Classification::Precompute;
		if (meta.classificationKnown && meta.isPrecompute)
		{
			// MC26.8 AAttrList::ConvertAttributesFromAAF (0x17a2e0,
			// 0x17a464) turns the exact string __AttributeList plus its
			// TaggedValueAttributeList into AddObject, i.e. the kind3 tested
			// by GetImportSettingAttrList. A recursively found tag NAME is
			// insufficient: inspect only this master's direct MobAttributeList.
			// Unlike the recovery graph walker, decisive lists must be complete.
			const auto strictReferences = [&](const QByteArray &value, QVector<int> &targets) {
				if (value.size() < 8 || readUint32BE(value, 4) != 16) return false;
				const quint32 count = readUint32BE(value, 0);
				if (quint64(count) * 16 != quint64(value.size() - 8)) return false;
				QSet<int> unique;
				for (quint32 i = 0; i < count; ++i)
				{
					const int target = byInstance.value(value.mid(8 + qsizetype(i) * 16, 16), -1);
					if (target < 0 || unique.contains(target)) return false;
					unique.insert(target);
					targets.append(target);
				}
				return true;
			};
			const auto exactText = [](const QByteArray &value, bool little, QString &text) {
				if (value.size() < 2 || value.size() % 2 != 0) return false;
				for (qsizetype i = 0; i < value.size(); i += 2)
				{
					const auto *p = reinterpret_cast<const uchar *>(value.constData() + i);
					const quint16 c = little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
					if (c == 0) return i + 2 == value.size();
					if (QChar(c).isSurrogate()) return false;
					text.append(QChar(c));
				}
				return true;
			};
			const auto importEvidence = [&] {
				using Attribute = AvidPrecompute::ImportAttribute;
				if (!set.fields.contains(0xf001)) return Attribute::Absent;
				if (!set.identifiedProperties.contains(0xf001)) return Attribute::Unknown;
				QVector<int> attributes;
				if (!strictReferences(set.fields.value(0xf001), attributes)) return Attribute::Unknown;
				bool found = false;
				Attribute verdict = Attribute::Absent;
				for (int index : attributes)
				{
					const auto &attribute = sets[index];
					QString name;
					if (attribute.type != kSetTaggedValue ||
						!exactText(attribute.fields.value(0x5001), false, name)) return Attribute::Unknown;
					if (name != QLatin1String("_IMPORTSETTING")) continue;
					if (found) return Attribute::Conflicting; // Do not choose among duplicate definitions.
					found = true;
					const QByteArray value = attribute.fields.value(0x5003);
					if (value.size() < 17 || (value[0] != 'L' && value[0] != 'B')) return Attribute::Unknown;
					const bool little = value[0] == 'L';
					const QByteArray type = value.mid(1, 16);
					const QByteArray stringType = QByteArray::fromHex(little ?
						"0002100100000000060e2b3401040101" : "0110020000000000060e2b3401040101");
					if (type != stringType) return Attribute::Unknown;
					QString payload;
					if (!exactText(value.mid(17), little, payload)) return Attribute::Unknown;
					if (payload == QLatin1String("__PortableObject")) return Attribute::Unknown;
					if (payload != QLatin1String("__AttributeList")) continue; // An ordinary string is not kind3.
					QVector<int> children;
					if (!attribute.identifiedProperties.contains(0xf002) ||
						!strictReferences(attribute.fields.value(0xf002), children)) return Attribute::Unknown;
					for (int child : children)
						if (sets[child].type != kSetTaggedValue) return Attribute::Unknown;
					verdict = Attribute::Present;
				}
				return verdict;
			};
			AvidPrecompute::Evidence evidence;
			evidence.importAttribute = importEvidence();
			if (evidence.importAttribute == AvidPrecompute::ImportAttribute::Present)
			{
				QVector<int> tracks;
				bool complete = strictReferences(set.fields.value(0x4403), tracks);
				int videos = 0;
				// MC GetTrackTypeFromDDEF (0x2265c) maps both registered and
				// legacy Picture IDs to GetType()==1. Count the immediate slot
				// segment, never its nested sources or the physical essence kind.
				static const QSet<QByteArray> picture = {
					QByteArray::fromHex("060e2b34040101010103020201000000"),
					QByteArray::fromHex("807d006008143e6f6f3c8ce16cef11d2")};
				static const QSet<QByteArray> nonPicture = {
					QByteArray::fromHex("060e2b34040101010103020202000000"), // Sound
					QByteArray::fromHex("807d006008143e6f78e1ebe16cef11d2"), // Legacy Sound
					QByteArray::fromHex("060e2b34040101010103020101000000")}; // Timecode
				for (int track : tracks)
				{
					const auto &slot = sets[track];
					const QByteArray reference = slot.fields.value(0x4803);
					const int component = reference.size() == 16 ? byInstance.value(reference, -1) : -1;
					if ((slot.type != 0x39 && slot.type != 0x3a && slot.type != 0x3b) || component < 0)
					{
						complete = false;
						break;
					}
					const auto componentType = sets[component].type;
					if (componentType != kSetSequence && componentType != kSetSourceClip && componentType != kSetTimecode)
					{
						complete = false; // An unsupported segment cannot supply a verified track kind.
						break;
					}
					const QByteArray kind = sets[component].fields.value(0x0201);
					if (picture.contains(kind)) ++videos;
					else if (!nonPicture.contains(kind)) complete = false;
				}
				if (complete) evidence.videoTrackCount = videos;
			}
			meta.precomputeCategory = AvidPrecompute::classify(evidence);
		}
	}
	else if (filePackage >= 0)
	{
		const auto &set = sets[filePackage];
		parsePackage(set.local, 0, set.local.size(), meta, false);
	}
	else if (materials.isEmpty())
	{
		for (const auto &set : sets)
			if (set.type == kSetSrcPkg) { parsePackage(set.local, 0, set.local.size(), meta, false); break; }
	}
	QVector<int> chosenDescriptors;
	if (filePackage >= 0)
	{
		for (int descriptor : refs(sets[filePackage].fields.value(0x4701)))
		{
			if (isDescriptor(sets[descriptor].type)) chosenDescriptors.append(descriptor);
			else if (sets[descriptor].type == 0x44)
				for (int child : refs(sets[descriptor].fields.value(0x3f01)))
					if (isDescriptor(sets[child].type)) chosenDescriptors.append(child);
		}
	}
	else if (files.isEmpty() && descriptors.size() == 1)
		chosenDescriptors = descriptors; // standalone/older header recovery
	// A row has one essence description. Never combine width from one
	// descriptor with rate/compression from another. Multiplexed picture+sound
	// uses its unique picture descriptor; multiple pictures remain unresolved.
	int chosen = chosenDescriptors.size() == 1 ? chosenDescriptors.first() : -1;
	if (chosenDescriptors.size() > 1)
	{
		for (int candidate : chosenDescriptors)
			if (sets[candidate].type == kSetCdci || sets[candidate].type == kSetRgba || sets[candidate].type == 0x51)
			{
				if (chosen >= 0) { chosen = -1; break; }
				chosen = candidate;
			}
	}
	if (chosen >= 0)
	{
		const auto &set = sets[chosen];
		meta.isAudio = set.type == kSetWave || set.type == kSetAes3 || set.type == kSetSoundMpeg || set.type == 0x42;
		meta.pcmDescriptor = set.type == kSetWave || set.type == kSetAes3;
		meta.rgbaDescriptor = set.type == kSetRgba;
		parseDescriptorSet(set.local, 0, set.local.size(), meta);
	}
	QSet<int> scope;
	if (material >= 0) scope = descendants(material, true);
	else if (filePackage >= 0) scope = descendants(filePackage, true);
	// Standalone metadata lacks graph edges. Retain recovery only when no
	// package declares an object graph, rather than pooling unrelated tracks.
	const bool graphDeclared = (material >= 0 && sets[material].fields.contains(0x4403)) ||
		(filePackage >= 0 && sets[filePackage].fields.contains(0x4403));
	if (!graphDeclared)
		for (int n = 0; n < sets.size(); ++n) scope.insert(n);
	for (int n = 0; n < sets.size(); ++n)
	{
		if (!scope.contains(n)) continue;
		const auto &set = sets[n];
		if (!graphDeclared && (set.type == kSetSequence || set.type == kSetSourceClip || set.type == kSetTimecode))
			parseStructuralComponent(set.local, 0, set.local.size(), meta);
		else if (set.type == kSetTaggedValue)
			parseTaggedValue(set.local, 0, set.local.size(), meta);
	}
	// Projects belong to packages. An older source's _PJ may precede the
	// owning file's _PJ on disk, so the first tagged value is not authority.
	// Retain the scoped import fields above, then choose the project from
	// the file's own attributes, the material's own attributes, or its linked
	// source ancestry. A set of conflicting candidates remains unknown.
	meta.projectName.clear();
	auto projectsIn = [&](const QSet<int> &indices) {
		QSet<QString> projects;
		for (int index : indices)
		{
			const auto &set = sets[index];
			if (set.type != kSetTaggedValue) continue;
			MxfMetadata attribute;
			parseTaggedValue(set.local, 0, set.local.size(), attribute);
			if (!attribute.projectName.isEmpty()) projects.insert(attribute.projectName);
		}
		return projects;
	};
	auto ownProjects = [&](int package) {
		QSet<int> attributes;
		if (package < 0) return QSet<QString>{};
		QVector<int> queue;
		for (quint16 tag : {quint16(0x4406), quint16(0xf001)})
			for (int index : refs(sets[package].fields.value(tag))) queue.append(index);
		for (qsizetype n = 0; n < queue.size(); ++n)
		{
			const int index = queue[n];
			if (attributes.contains(index) || sets[index].type != kSetTaggedValue) continue;
			attributes.insert(index);
			for (int child : refs(sets[index].fields.value(0xf002))) queue.append(child);
		}
		return projectsIn(attributes);
	};
	QSet<QString> projects = ownProjects(filePackage);
	if (projects.isEmpty()) projects = ownProjects(material);
	if (projects.isEmpty())
	{
		const int root = filePackage >= 0 ? filePackage : material;
		if (root >= 0)
			for (int index : descendants(root, true))
				if (sets[index].type == kSetSrcPkg && index != filePackage)
					projects.unite(ownProjects(index));
	}
	if (projects.isEmpty())
	{
		// Some older render graphs contain structural wrappers we cannot yet
		// traverse. Recover a project only if every readable _PJ/PROJNAME in
		// this header agrees; never choose an arbitrary unrelated package.
		QSet<int> all;
		for (int n = 0; n < sets.size(); ++n) all.insert(n);
		projects = projectsIn(all);
	}
	if (projects.size() == 1) meta.projectName = *projects.constBegin();
	// Durations belong to tracks, not to arbitrary descendant components.
	// In particular a Sequence(250) containing two SourceClips(125) is250,
	// and an audio track's sample units must be converted to display frames.
	if (material >= 0 && graphDeclared)
	{
		struct TrackTime { int component; double rate; qint64 duration; bool ownsFile; };
		QVector<TrackTime> timing;
		const QByteArray fileId = filePackage >= 0 ? sets[filePackage].fields.value(0x4401) : QByteArray{};
		const QByteArray linkedTrack = chosen >= 0 ? sets[chosen].fields.value(0x3006) : QByteArray{};
		for (int track : refs(sets[material].fields.value(0x4403)))
		{
			const QByteArray rate = sets[track].fields.value(0x4b01);
			const auto components = refs(sets[track].fields.value(0x4803));
			if (rate.size() != 8 || components.size() != 1) continue;
			const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
			if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX)) continue;
			const int component = components.first();
			const QByteArray duration = sets[component].fields.value(0x0202);
			const qint64 length = readDuration(duration, 0, quint16(qMin<qsizetype>(duration.size(), 65535)));
			bool owns = false;
			for (int descendant : descendants(component, false))
			{
				const auto &child = sets[descendant];
				if (child.type == kSetSourceClip && !fileId.isEmpty() && child.fields.value(0x1101) == fileId &&
					(linkedTrack.isEmpty() || child.fields.value(0x1102) == linkedTrack))
					owns = true;
				if (child.type == kSetTimecode && child.fields.value(0x1503).size() == 1)
					meta.dropFrame = meta.dropFrame || child.fields.value(0x1503)[0] != 0;
			}
			timing.append({component, double(num) / den, length, owns});
		}
		const TrackTime *owning = nullptr;
		for (const auto &track : timing)
			if (track.ownsFile)
			{
				if (owning) { owning = nullptr; break; }
				owning = &track;
			}
		if (!owning && filePackage < 0 && timing.size() == 1) owning = &timing.first();
		double displayRate = owning && owning->rate < 1000.0 ? owning->rate : 0.0;
		if (displayRate == 0.0)
			for (const auto &track : timing)
				if (track.rate >= 1.0 && track.rate < 1000.0)
				{
					if (displayRate > 0 && qAbs(displayRate - track.rate) > 0.00001) { displayRate = 0; break; }
					displayRate = track.rate;
				}
		// Audio-only material packages can have sample-rate tracks only.
		// Their linked source/timecode tracks establish the project's frame
		// rate; use it only when that ancestry offers one consistent rate.
		if (displayRate == 0.0 && owning && owning->rate >= 1000.0)
		{
			for (int index : scope)
			{
				const QByteArray rate = sets[index].fields.value(0x4b01);
				if (rate.size() != 8) continue;
				const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
				if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX)) continue;
				const double candidate = double(num) / den;
				if (candidate < 1.0 || candidate >= 1000.0) continue;
				if (displayRate > 0 && qAbs(displayRate - candidate) > 0.00001) { displayRate = 0; break; }
				displayRate = candidate;
			}
		}
		if (displayRate > 0)
			meta.timecodeBase = qRound(displayRate);
		// Avid can put the timecode track on the linked source package.
		// Prefer the material package's timecode; consult its ancestry only
		// when absent, and require a consistent drop-frame flag at this base.
		const auto readDropFrame = [&](const QSet<int> &candidates) {
			int flag = -1;
			for (int index : candidates)
			{
				const auto &set = sets[index];
				const QByteArray base = set.fields.value(0x1502);
				const QByteArray drop = set.fields.value(0x1503);
				if (set.type != kSetTimecode || base.size() != 2 || drop.size() != 1 ||
					readUint16BE(base, 0) != meta.timecodeBase) continue;
				const int next = drop[0] != 0;
				if (flag >= 0 && flag != next) return -2;
				flag = next;
			}
			return flag;
		};
		int drop = readDropFrame(descendants(material, false));
		if (drop == -1) drop = readDropFrame(scope);
		meta.dropFrame = drop == 1;

		// The MaterialPackage can concatenate several physical files. Its
		// full sequence length belongs to the master, not to each file row.
		// The selected descriptor measures the stored essence; even the file
		// track can hold a one-frame title for thousands of frames. Fall back
		// to that track only when the descriptor lacks a usable duration/rate.
		// Keep edit units paired with their rate for audio sample conversion.
		qint64 fileDuration = 0;
		double fileRate = 0.0;
		if (chosen >= 0 && meta.descriptorDuration > 0)
		{
			const QByteArray rate = sets[chosen].fields.value(0x3001);
			if (rate.size() == 8)
			{
				const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
				if (num > 0 && den > 0 && num <= quint32(INT_MAX) && den <= quint32(INT_MAX))
				{
					fileDuration = meta.descriptorDuration;
					fileRate = double(num) / den;
				}
			}
		}
		if (fileDuration == 0 && filePackage >= 0)
		{
			bool ambiguous = false;
			for (int track : refs(sets[filePackage].fields.value(0x4403)))
			{
				if (!linkedTrack.isEmpty() && sets[track].fields.value(0x4801) != linkedTrack) continue;
				const QByteArray rate = sets[track].fields.value(0x4b01);
				const auto components = refs(sets[track].fields.value(0x4803));
				if (rate.size() != 8 || components.size() != 1 || sets[components.first()].type == kSetTimecode) continue;
				const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
				if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX)) continue;
				const QByteArray duration = sets[components.first()].fields.value(0x0202);
				const qint64 length = readDuration(duration, 0, quint16(qMin<qsizetype>(duration.size(), 65535)));
				if (length <= 0) continue;
				if (fileDuration > 0) { ambiguous = true; break; }
				fileDuration = length;
				fileRate = double(num) / den;
			}
			if (ambiguous) { fileDuration = 0; fileRate = 0; }
		}
		bool materialDescribesOnlyThisFile = owning != nullptr;
		if (owning && filePackage >= 0)
			for (int index : descendants(owning->component, false))
				if (sets[index].type == kSetSourceClip && sets[index].fields.value(0x1101) != fileId)
					materialDescribesOnlyThisFile = false;
		if (fileDuration == 0 && materialDescribesOnlyThisFile)
		{
			fileDuration = owning->duration;
			fileRate = owning->rate;
		}
		if (fileDuration > 0 && fileRate > 0 && displayRate > 0)
		{
			const double frames = double(fileDuration) * displayRate / fileRate;
			if (frames >= 1.0 && frames < double(std::numeric_limits<qint64>::max()))
			{
				meta.durationFrames = qRound64(frames);
				meta.durationFromTrack = true;
			}
		}
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
		meta.height = meta.height > 0 && meta.height <= std::numeric_limits<int>::max() / 2 ? meta.height * 2 : 0;
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
	if (meta.valid && meta.codec.isEmpty() && !meta.essenceContainerLabel.isEmpty())
		meta.codec = codecFromEssenceLabel(meta.essenceContainerLabel, meta.fps);
	if (meta.valid && meta.codec.isEmpty() && meta.isAudio && meta.pcmDescriptor)
		meta.codec = QString::fromLatin1(kPcmAudioName);

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
		case 0x3004: // container/wrapping UL is not the picture/sound coding UL
			if (len == 16)
				out.wrappingLabel = data.mid(pos, len);
			break;
		case 0x3201: // picture essence coding UL — identifies the codec
			out.pictureCodingPresent = true;
			if (len >= 8 && out.essenceContainerLabel.isEmpty())
				out.essenceContainerLabel = data.mid(pos, len);
			break;
		case 0x3401: // eight Code:Depth pairs; A is alpha, zero ends the layout
			if (out.rgbaDescriptor && len == 16 && data[pos] == 'A' && quint8(data[pos + 1]) == 8 &&
				data[pos + 2] == '\0' && data[pos + 3] == '\0')
			{
				bool alphaOnly = true;
				for (int i = 2; i < 16; i += 2)
					if ((data[pos + i] != '\0' && data[pos + i] != '0') || data[pos + i + 1] != '\0')
						alphaOnly = false;
				out.rgbaAlpha8 = alphaOnly;
				if (alphaOnly) out.bitDepth = QStringLiteral("8-bit");
			}
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
	// `_PJ` is the attribute Media Composer's own PMR rebuild asks the mob for
	// (`PROJNAME` is its legacy spelling). Packages can name different projects;
	// parseFromBuffer resolves ownership after decoding individual candidates.
	const bool wantProject = (name == QLatin1String("_PJ") || name == QLatin1String("PROJNAME")) &&
							 out.projectName.isEmpty();
	if (!wantPath && !wantContainer && !wantProject)
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
	else if (wantContainer)
		out.sourceContainer = text;
	else
		out.projectName = text;
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
