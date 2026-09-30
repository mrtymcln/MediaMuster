#include "mxfparser.h"
#include "avidusage.h"
#include "diagnostics.h"
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
	case 0x0f:
	case 0x11:
	case 0x14:
	case 0x18:
	case 0x23:
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2f:
	case 0x32:
	case 0x36:
	case 0x37:
	case 0x39:
	case 0x3a:
	case 0x3b:
	case 0x3f:
	case 0x42:
	case 0x44:
	case 0x47:
	case 0x48:
	case 0x51:
	case 0x5e:
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

/// Read a big-endian duration encoded in 4–8 bytes. Other lengths and
/// values above qint64's maximum return -1. The caller must have checked
/// that the whole field fits inside the buffer.
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
MediaMetadata MxfParser::parseHeader(const QString &mediaFilePath, qint64 *bytesRead)
{
	if (bytesRead)
		*bytesRead = 0;
	QFile file(mediaFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		MediaMetadata result;
		result.headerStatus = MediaMetadata::HeaderStatus::IoError;
		qCWarning(lcMxf) << "cannot open" << mediaFilePath << file.errorString();
		return result;
	}
	return parseHeader(file, bytesRead);
}

MediaMetadata MxfParser::parseHeader(QFile &file, qint64 *bytesRead)
{
	using Status = MediaMetadata::HeaderStatus;
	qint64 readCount = 0;
	if (bytesRead)
		*bytesRead = 0;
	if (!file.isOpen() || !file.isReadable() || file.isSequential() || !file.seek(0))
	{
		MediaMetadata result;
		result.headerStatus = Status::IoError;
		return result;
	}
	auto read = [&](qint64 count)
	{
		QByteArray result = file.read(count);
		readCount += result.size();
		return result;
	};
	// SMPTE ST 377-1:2019 section 6.6 limits the run-in before the header
	// partition to less than 65536 bytes. This is a format constraint, not
	// a limit on the file or its metadata; retain the one-byte tolerance.
	constexpr qint64 kRunInLimit = 64 * 1024;
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
	while (status == Status::Complete && !file.atEnd())
	{
		if (metadataEnd >= 0 && file.pos() >= metadataEnd)
			break;
		const qint64 keyStart = file.pos();
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
				if (partitionEssenceContainer.size() != 16)
					status = Status::Incomplete;
			}
			else if (!file.seek(file.pos() + length - 88))
				status = Status::IoError;
		}
		else if (isSet || key == primerKey)
		{
			// The accumulated buffer must remain representable by Qt's index
			// type, including its trailing NUL; there is no policy size ceiling.
			if (length > std::numeric_limits<qsizetype>::max() - 1 - metadata.size() - key.size() - ber.size())
			{
				status = Status::Malformed;
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
		else if (length > 0 && !file.seek(file.pos() + length))
			status = Status::IoError;
	}
	if (status == Status::Complete && metadataEnd > file.size() && !shortFinalFill)
		status = Status::Incomplete;
	MediaMetadata result = parseHeaderMetadata(metadata);
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
		qCWarning(lcMxf) << "no complete usable MXF metadata in" << file.fileName()
						 << "status" << int(status) << "read" << readCount << "bytes";
	if (bytesRead)
		*bytesRead = readCount;
	return result;
}

// MARK: - KLV walk

MediaMetadata MxfParser::parseHeaderMetadata(const QByteArray &data)
{
	struct MxfObject
	{
		quint8 type = 0;
		QByteArray fieldData;
		QHash<quint16, QByteArray> fields;
		QSet<quint16> identifiedProperties; // Primer-confirmed private property identity.
	};
	MediaMetadata meta;
	QVector<MxfObject> objects;
	QHash<quint16, quint16> primer;
	QVector<QPair<quint8, QByteArray>> rawSets;
	const QByteArray primerKey = QByteArray::fromHex("060e2b34020501010d01020101050100");
	static const QHash<QByteArray, quint16> properties = []
	{
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
	auto fail = [&]
	{
		MediaMetadata result;
		result.headerStatus = MediaMetadata::HeaderStatus::Malformed;
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
	QHash<QByteArray, qsizetype> objectIndexByInstanceUid;
	QHash<QByteArray, qsizetype> packageIndexByMobId;
	QVector<qsizetype> materials, files, descriptors;
	auto isDescriptor = [](quint8 type)
	{
		return type == kSetCdci || type == kSetRgba || type == kSetWave ||
			   type == kSetAes3 || type == kSetSoundMpeg || type == 0x27 || type == 0x42 || type == 0x51;
	};
	for (const auto &raw : rawSets)
	{
		MxfObject set;
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
			const quint16 tag = localTag == AvidUsage::kPrivateMxfTag && !primer.contains(localTag) ? 0 : primer.value(localTag, localTag);
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
				if (primer.contains(localTag))
					set.identifiedProperties.insert(tag);
				set.fieldData += char(tag >> 8);
				set.fieldData += char(tag & 0xff);
				set.fieldData += char(size >> 8);
				set.fieldData += char(size & 0xff);
				set.fieldData += value;
			}
			p += size;
		}
		const qsizetype index = objects.size();
		const QByteArray instance = set.fields.value(0x3c0a);
		if (instance.size() == 16)
		{
			if (objectIndexByInstanceUid.contains(instance))
				return fail();
			objectIndexByInstanceUid.insert(instance, index);
		}
		if (set.type == kSetMatPkg)
			materials.append(index);
		if (set.type == kSetSrcPkg && set.fields.contains(0x4701))
			files.append(index);
		if (set.type == kSetMatPkg || set.type == kSetSrcPkg)
			packageIndexByMobId.insert(set.fields.value(0x4401), index);
		if (isDescriptor(set.type))
			descriptors.append(index);
		objects.append(std::move(set));
	}
	// Decode references only when their shape is valid. Bounded traversal also
	// handles cycles and shared objects without recursive stack growth.
	auto resolveObjectReferences = [&](const QByteArray &value)
	{
		QVector<qsizetype> result;
		if (value.size() == 16)
		{
			const auto found = objectIndexByInstanceUid.constFind(value);
			if (found != objectIndexByInstanceUid.constEnd())
				result.append(found.value());
		}
		else if (value.size() >= 8)
		{
			const quint32 count = readUint32BE(value, 0), stride = readUint32BE(value, 4);
			if (stride == 16 && count == quint64(value.size() - 8) / 16 && (value.size() - 8) % 16 == 0)
				for (quint32 n = 0; n < count; ++n)
				{
					const auto found = objectIndexByInstanceUid.constFind(value.mid(8 + qsizetype(n) * 16, 16));
					if (found != objectIndexByInstanceUid.constEnd())
						result.append(found.value());
				}
		}
		return result;
	};
	auto reachableObjectIndexes = [&](qsizetype root, bool followSource)
	{
		QSet<qsizetype> visited;
		QVector<qsizetype> queue{root};
		for (qsizetype n = 0; n < queue.size(); ++n)
		{
			const qsizetype index = queue[n];
			if (index < 0 || visited.contains(index))
				continue;
			visited.insert(index);
			for (const auto &value : objects[index].fields)
				for (qsizetype target : resolveObjectReferences(value))
					if (!visited.contains(target))
						queue.append(target);
			if (followSource && objects[index].type == kSetSourceClip)
			{
				const auto target = packageIndexByMobId.constFind(objects[index].fields.value(0x1101));
				if (target != packageIndexByMobId.constEnd() && !visited.contains(target.value()))
					queue.append(target.value());
			}
		}
		return visited;
	};
	qsizetype filePackageIndex = -1;
	// EssenceContainerData explicitly identifies the package for the stored
	// essence. Prefer it over an unrelated/tape SourcePackage in the header.
	QSet<qsizetype> linkedFiles;
	for (const auto &set : objects)
		if (set.type == 0x23)
		{
			const qsizetype candidate = packageIndexByMobId.value(set.fields.value(0x2701), -1);
			if (files.contains(candidate))
				linkedFiles.insert(candidate);
		}
	if (linkedFiles.size() == 1)
		filePackageIndex = *linkedFiles.constBegin();
	else if (linkedFiles.isEmpty() && files.size() == 1)
		filePackageIndex = files.first();
	qsizetype materialPackageIndex = -1;
	if (filePackageIndex >= 0)
	{
		const QByteArray id = objects[filePackageIndex].fields.value(0x4401);
		if (id.size() == MobId::kRawSize)
			meta.fileMobId = MobId::format(reinterpret_cast<const unsigned char *>(id.constData()));
		for (qsizetype candidate : materials)
			if (reachableObjectIndexes(candidate, true).contains(filePackageIndex))
			{
				if (materialPackageIndex >= 0)
				{
					materialPackageIndex = -2;
					break;
				}
				materialPackageIndex = candidate;
			}
	}
	if (materialPackageIndex == -1 && materials.size() == 1)
		materialPackageIndex = materials.first();
	// An explicit Preface primary-package reference resolves otherwise
	// ambiguous connected material packages.
	QSet<qsizetype> primaryMaterials;
	for (const auto &set : objects)
		if (set.type == 0x2f)
			for (qsizetype candidate : resolveObjectReferences(set.fields.value(0x3b08)))
				if (materials.contains(candidate) && (filePackageIndex < 0 || reachableObjectIndexes(candidate, true).contains(filePackageIndex)))
					primaryMaterials.insert(candidate);
	if (primaryMaterials.size() == 1)
		materialPackageIndex = *primaryMaterials.constBegin();
	if (materialPackageIndex >= 0)
	{
		const auto &set = objects[materialPackageIndex];
		parsePackage(set.fieldData, 0, set.fieldData.size(), meta, true);
		const auto privateUsage = set.fields.constFind(AvidUsage::kPrivateMxfTag);
		const qint32 code = privateUsage == set.fields.cend() ? AvidUsage::kMissing : AvidUsage::integerCode(readUint32BE(privateUsage.value(), 0));
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
			const auto strictReferences = [&](const QByteArray &value, QVector<qsizetype> &targets)
			{
				if (value.size() < 8 || readUint32BE(value, 4) != 16)
					return false;
				const quint32 count = readUint32BE(value, 0);
				if (quint64(count) * 16 != quint64(value.size() - 8))
					return false;
				QSet<qsizetype> unique;
				for (quint32 i = 0; i < count; ++i)
				{
					const qsizetype target = objectIndexByInstanceUid.value(value.mid(8 + qsizetype(i) * 16, 16), -1);
					if (target < 0 || unique.contains(target))
						return false;
					unique.insert(target);
					targets.append(target);
				}
				return true;
			};
			const auto exactText = [](const QByteArray &value, bool little, QString &text)
			{
				if (value.size() < 2 || value.size() % 2 != 0)
					return false;
				for (qsizetype i = 0; i < value.size(); i += 2)
				{
					const auto *p = reinterpret_cast<const uchar *>(value.constData() + i);
					const quint16 c = little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
					if (c == 0)
						return i + 2 == value.size();
					if (QChar(c).isSurrogate())
						return false;
					text.append(QChar(c));
				}
				return true;
			};
			const auto importEvidence = [&]
			{
				using Attribute = AvidPrecompute::ImportAttribute;
				if (!set.fields.contains(0xf001))
					return Attribute::Absent;
				if (!set.identifiedProperties.contains(0xf001))
					return Attribute::Unknown;
				QVector<qsizetype> attributes;
				if (!strictReferences(set.fields.value(0xf001), attributes))
					return Attribute::Unknown;
				bool found = false;
				Attribute verdict = Attribute::Absent;
				for (qsizetype index : attributes)
				{
					const auto &attribute = objects[index];
					QString name;
					if (attribute.type != kSetTaggedValue ||
						!exactText(attribute.fields.value(0x5001), false, name))
						return Attribute::Unknown;
					if (name != QLatin1String("_IMPORTSETTING"))
						continue;
					if (found)
						return Attribute::Conflicting; // Do not choose among duplicate definitions.
					found = true;
					const QByteArray value = attribute.fields.value(0x5003);
					if (value.size() < 17 || (value[0] != 'L' && value[0] != 'B'))
						return Attribute::Unknown;
					const bool little = value[0] == 'L';
					const QByteArray type = value.mid(1, 16);
					const QByteArray stringType = QByteArray::fromHex(little ? "0002100100000000060e2b3401040101" : "0110020000000000060e2b3401040101");
					if (type != stringType)
						return Attribute::Unknown;
					QString payload;
					if (!exactText(value.mid(17), little, payload))
						return Attribute::Unknown;
					if (payload == QLatin1String("__PortableObject"))
						return Attribute::Unknown;
					if (payload != QLatin1String("__AttributeList"))
						continue; // An ordinary string is not kind3.
					QVector<qsizetype> children;
					if (!attribute.identifiedProperties.contains(0xf002) ||
						!strictReferences(attribute.fields.value(0xf002), children))
						return Attribute::Unknown;
					for (qsizetype child : children)
						if (objects[child].type != kSetTaggedValue)
							return Attribute::Unknown;
					verdict = Attribute::Present;
				}
				return verdict;
			};
			AvidPrecompute::Evidence evidence;
			evidence.importAttribute = importEvidence();
			if (evidence.importAttribute == AvidPrecompute::ImportAttribute::Present)
			{
				QVector<qsizetype> tracks;
				bool complete = strictReferences(set.fields.value(0x4403), tracks);
				int videos = 0;
				// MC GetTrackTypeFromDDEF (0x2265c) maps both registered and
				// legacy Picture IDs to GetType()==1. Count the immediate slot
				// segment, never its nested sources or the physical essence kind.
				static const QSet<QByteArray> picture = {
					QByteArray::fromHex("060e2b34040101010103020201000000"),
					QByteArray::fromHex("807d006008143e6f6f3c8ce16cef11d2")};
				static const QSet<QByteArray> nonPicture = {
					QByteArray::fromHex("060e2b34040101010103020202000000"),  // Sound
					QByteArray::fromHex("807d006008143e6f78e1ebe16cef11d2"),  // Legacy Sound
					QByteArray::fromHex("060e2b34040101010103020101000000")}; // Timecode
				for (qsizetype track : tracks)
				{
					const auto &slot = objects[track];
					const QByteArray reference = slot.fields.value(0x4803);
					const qsizetype component = reference.size() == 16 ? objectIndexByInstanceUid.value(reference, -1) : -1;
					if ((slot.type != 0x39 && slot.type != 0x3a && slot.type != 0x3b) || component < 0)
					{
						complete = false;
						break;
					}
					const auto componentType = objects[component].type;
					if (componentType != kSetSequence && componentType != kSetSourceClip && componentType != kSetTimecode)
					{
						complete = false; // An unsupported segment cannot supply a verified track kind.
						break;
					}
					const QByteArray kind = objects[component].fields.value(0x0201);
					if (picture.contains(kind))
						++videos;
					else if (!nonPicture.contains(kind))
						complete = false;
				}
				if (complete)
					evidence.videoTrackCount = videos;
			}
			meta.precomputeCategory = AvidPrecompute::classify(evidence);
		}
	}
	else if (filePackageIndex >= 0)
	{
		const auto &set = objects[filePackageIndex];
		parsePackage(set.fieldData, 0, set.fieldData.size(), meta, false);
	}
	else if (materials.isEmpty())
	{
		for (const auto &set : objects)
			if (set.type == kSetSrcPkg)
			{
				parsePackage(set.fieldData, 0, set.fieldData.size(), meta, false);
				break;
			}
	}
	QVector<qsizetype> chosenDescriptors;
	if (filePackageIndex >= 0)
	{
		for (qsizetype descriptor : resolveObjectReferences(objects[filePackageIndex].fields.value(0x4701)))
		{
			if (isDescriptor(objects[descriptor].type))
				chosenDescriptors.append(descriptor);
			else if (objects[descriptor].type == 0x44)
				for (qsizetype child : resolveObjectReferences(objects[descriptor].fields.value(0x3f01)))
					if (isDescriptor(objects[child].type))
						chosenDescriptors.append(child);
		}
	}
	else if (files.isEmpty() && descriptors.size() == 1)
		chosenDescriptors = descriptors; // standalone/older header recovery
	// A row has one essence description. Never combine width from one
	// descriptor with rate/compression from another. Multiplexed picture+sound
	// uses its unique picture descriptor; multiple pictures remain unresolved.
	qsizetype descriptorIndex = chosenDescriptors.size() == 1 ? chosenDescriptors.first() : -1;
	if (chosenDescriptors.size() > 1)
	{
		for (qsizetype candidate : chosenDescriptors)
			if (objects[candidate].type == kSetCdci || objects[candidate].type == kSetRgba || objects[candidate].type == 0x51)
			{
				if (descriptorIndex >= 0)
				{
					descriptorIndex = -1;
					break;
				}
				descriptorIndex = candidate;
			}
	}
	if (descriptorIndex >= 0)
	{
		const auto &set = objects[descriptorIndex];
		meta.isAudio = set.type == kSetWave || set.type == kSetAes3 || set.type == kSetSoundMpeg || set.type == 0x42;
		meta.pcmDescriptor = set.type == kSetWave || set.type == kSetAes3;
		meta.rgbaDescriptor = set.type == kSetRgba;
		parseDescriptorSet(set.fieldData, 0, set.fieldData.size(), meta);
	}
	QSet<qsizetype> scope;
	if (materialPackageIndex >= 0)
		scope = reachableObjectIndexes(materialPackageIndex, true);
	else if (filePackageIndex >= 0)
		scope = reachableObjectIndexes(filePackageIndex, true);
	// Standalone metadata lacks graph edges. Retain recovery only when no
	// package declares an object graph, rather than pooling unrelated tracks.
	const bool graphDeclared = (materialPackageIndex >= 0 && objects[materialPackageIndex].fields.contains(0x4403)) ||
							   (filePackageIndex >= 0 && objects[filePackageIndex].fields.contains(0x4403));
	if (!graphDeclared)
		for (qsizetype n = 0; n < objects.size(); ++n)
			scope.insert(n);
	for (qsizetype n = 0; n < objects.size(); ++n)
	{
		if (!scope.contains(n))
			continue;
		const auto &set = objects[n];
		if (!graphDeclared && (set.type == kSetSequence || set.type == kSetSourceClip || set.type == kSetTimecode))
			parseStructuralComponent(set.fieldData, 0, set.fieldData.size(), meta);
		else if (set.type == kSetTaggedValue)
			parseTaggedValue(set.fieldData, 0, set.fieldData.size(), meta);
	}
	// Projects belong to packages. An older source's _PJ may precede the
	// owning file's _PJ on disk, so the first tagged value is not authority.
	// Retain the scoped import fields above, then choose the project from
	// the file's own attributes, the material's own attributes, or its linked
	// source ancestry. A set of conflicting candidates remains unknown.
	meta.projectName.clear();
	auto projectsIn = [&](const QSet<qsizetype> &indices)
	{
		QSet<QString> projects;
		for (qsizetype index : indices)
		{
			const auto &set = objects[index];
			if (set.type != kSetTaggedValue)
				continue;
			MediaMetadata attribute;
			parseTaggedValue(set.fieldData, 0, set.fieldData.size(), attribute);
			if (!attribute.projectName.isEmpty())
				projects.insert(attribute.projectName);
		}
		return projects;
	};
	auto ownProjects = [&](qsizetype package)
	{
		QSet<qsizetype> attributes;
		if (package < 0)
			return QSet<QString>{};
		QVector<qsizetype> queue;
		for (quint16 tag : {quint16(0x4406), quint16(0xf001)})
			for (qsizetype index : resolveObjectReferences(objects[package].fields.value(tag)))
				queue.append(index);
		for (qsizetype n = 0; n < queue.size(); ++n)
		{
			const qsizetype index = queue[n];
			if (attributes.contains(index) || objects[index].type != kSetTaggedValue)
				continue;
			attributes.insert(index);
			for (qsizetype child : resolveObjectReferences(objects[index].fields.value(0xf002)))
				queue.append(child);
		}
		return projectsIn(attributes);
	};
	QSet<QString> projects = ownProjects(filePackageIndex);
	if (projects.isEmpty())
		projects = ownProjects(materialPackageIndex);
	if (projects.isEmpty())
	{
		const qsizetype root = filePackageIndex >= 0 ? filePackageIndex : materialPackageIndex;
		if (root >= 0)
			for (qsizetype index : reachableObjectIndexes(root, true))
				if (objects[index].type == kSetSrcPkg && index != filePackageIndex)
					projects.unite(ownProjects(index));
	}
	if (projects.isEmpty())
	{
		// Some older render graphs contain structural wrappers we cannot yet
		// traverse. Recover a project only if every readable _PJ/PROJNAME in
		// this header agrees; never choose an arbitrary unrelated package.
		QSet<qsizetype> all;
		for (qsizetype n = 0; n < objects.size(); ++n)
			all.insert(n);
		projects = projectsIn(all);
	}
	if (projects.size() == 1)
		meta.projectName = *projects.constBegin();
	// Durations belong to tracks, not to arbitrary descendant components.
	// In particular a Sequence(250) containing two SourceClips(125) is250,
	// and an audio track's sample units must be converted to display frames.
	if (materialPackageIndex >= 0 && graphDeclared)
	{
		struct MxfMaterialTrack
		{
			qsizetype componentIndex;
			double editRate;
			qint64 durationUnits;
			bool referencesFile;
		};
		QVector<MxfMaterialTrack> timing;
		const QByteArray fileId = filePackageIndex >= 0 ? objects[filePackageIndex].fields.value(0x4401) : QByteArray{};
		const QByteArray linkedTrack = descriptorIndex >= 0 ? objects[descriptorIndex].fields.value(0x3006) : QByteArray{};
		for (qsizetype track : resolveObjectReferences(objects[materialPackageIndex].fields.value(0x4403)))
		{
			const QByteArray rate = objects[track].fields.value(0x4b01);
			const auto components = resolveObjectReferences(objects[track].fields.value(0x4803));
			if (rate.size() != 8 || components.size() != 1)
				continue;
			const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
			if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX))
				continue;
			const qsizetype component = components.first();
			const QByteArray duration = objects[component].fields.value(0x0202);
			const qint64 length = readDuration(duration, 0, quint16(qMin<qsizetype>(duration.size(), 65535)));
			bool owns = false;
			for (qsizetype descendant : reachableObjectIndexes(component, false))
			{
				const auto &child = objects[descendant];
				if (child.type == kSetSourceClip && !fileId.isEmpty() && child.fields.value(0x1101) == fileId &&
					(linkedTrack.isEmpty() || child.fields.value(0x1102) == linkedTrack))
					owns = true;
				if (child.type == kSetTimecode && child.fields.value(0x1503).size() == 1)
					meta.dropFrame = meta.dropFrame || child.fields.value(0x1503)[0] != 0;
			}
			timing.append({component, double(num) / den, length, owns});
		}
		const MxfMaterialTrack *owning = nullptr;
		for (const auto &track : timing)
			if (track.referencesFile)
			{
				if (owning)
				{
					owning = nullptr;
					break;
				}
				owning = &track;
			}
		if (!owning && filePackageIndex < 0 && timing.size() == 1)
			owning = &timing.first();
		double displayRate = owning && owning->editRate < 1000.0 ? owning->editRate : 0.0;
		if (displayRate == 0.0)
			for (const auto &track : timing)
				if (track.editRate >= 1.0 && track.editRate < 1000.0)
				{
					if (displayRate > 0 && qAbs(displayRate - track.editRate) > 0.00001)
					{
						displayRate = 0;
						break;
					}
					displayRate = track.editRate;
				}
		// Audio-only material packages can have sample-rate tracks only.
		// Their linked source/timecode tracks establish the project's frame
		// rate; use it only when that ancestry offers one consistent rate.
		if (displayRate == 0.0 && owning && owning->editRate >= 1000.0)
		{
			for (qsizetype index : scope)
			{
				const QByteArray rate = objects[index].fields.value(0x4b01);
				if (rate.size() != 8)
					continue;
				const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
				if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX))
					continue;
				const double candidate = double(num) / den;
				if (candidate < 1.0 || candidate >= 1000.0)
					continue;
				if (displayRate > 0 && qAbs(displayRate - candidate) > 0.00001)
				{
					displayRate = 0;
					break;
				}
				displayRate = candidate;
			}
		}
		if (displayRate > 0)
			meta.timecodeBase = qRound(displayRate);
		// Avid can put the timecode track on the linked source package.
		// Prefer the material package's timecode; consult its ancestry only
		// when absent, and require a consistent drop-frame flag at this base.
		const auto readDropFrame = [&](const QSet<qsizetype> &candidates)
		{
			int flag = -1;
			for (qsizetype index : candidates)
			{
				const auto &set = objects[index];
				const QByteArray base = set.fields.value(0x1502);
				const QByteArray drop = set.fields.value(0x1503);
				if (set.type != kSetTimecode || base.size() != 2 || drop.size() != 1 ||
					readUint16BE(base, 0) != meta.timecodeBase)
					continue;
				const int next = drop[0] != 0;
				if (flag >= 0 && flag != next)
					return -2;
				flag = next;
			}
			return flag;
		};
		int drop = readDropFrame(reachableObjectIndexes(materialPackageIndex, false));
		if (drop == -1)
			drop = readDropFrame(scope);
		meta.dropFrame = drop == 1;

		// The MaterialPackage can concatenate several physical files. Its
		// full sequence length belongs to the master, not to each file row.
		// The selected descriptor measures the stored essence; even the file
		// track can hold a one-frame title for thousands of frames. Fall back
		// to that track only when the descriptor lacks a usable duration/rate.
		// Keep edit units paired with their rate for audio sample conversion.
		qint64 fileDuration = 0;
		double fileRate = 0.0;
		if (descriptorIndex >= 0 && meta.descriptorDuration > 0)
		{
			const QByteArray rate = objects[descriptorIndex].fields.value(0x3001);
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
		if (fileDuration == 0 && filePackageIndex >= 0)
		{
			bool ambiguous = false;
			for (qsizetype track : resolveObjectReferences(objects[filePackageIndex].fields.value(0x4403)))
			{
				if (!linkedTrack.isEmpty() && objects[track].fields.value(0x4801) != linkedTrack)
					continue;
				const QByteArray rate = objects[track].fields.value(0x4b01);
				const auto components = resolveObjectReferences(objects[track].fields.value(0x4803));
				if (rate.size() != 8 || components.size() != 1 || objects[components.first()].type == kSetTimecode)
					continue;
				const quint32 num = readUint32BE(rate, 0), den = readUint32BE(rate, 4);
				if (num == 0 || den == 0 || num > quint32(INT_MAX) || den > quint32(INT_MAX))
					continue;
				const QByteArray duration = objects[components.first()].fields.value(0x0202);
				const qint64 length = readDuration(duration, 0, quint16(qMin<qsizetype>(duration.size(), 65535)));
				if (length <= 0)
					continue;
				if (fileDuration > 0)
				{
					ambiguous = true;
					break;
				}
				fileDuration = length;
				fileRate = double(num) / den;
			}
			if (ambiguous)
			{
				fileDuration = 0;
				fileRate = 0;
			}
		}
		bool materialDescribesOnlyThisFile = owning != nullptr;
		if (owning && filePackageIndex >= 0)
			for (qsizetype index : reachableObjectIndexes(owning->componentIndex, false))
				if (objects[index].type == kSetSourceClip && objects[index].fields.value(0x1101) != fileId)
					materialDescribesOnlyThisFile = false;
		if (fileDuration == 0 && materialDescribesOnlyThisFile)
		{
			fileDuration = owning->durationUnits;
			fileRate = owning->editRate;
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

	MediaMetadataUtil::finalise(meta);
	return meta;
}

void MxfParser::parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length,
								   MediaMetadata &out)
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
			if (len >= 8 && out.compressionLabel.isEmpty())
				out.compressionLabel = data.mid(pos, len);
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
				if (alphaOnly)
					out.bitDepth = QStringLiteral("8-bit");
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
				MediaMetadataUtil::applyEditRate(out, readUint32BE(data, pos), readUint32BE(data, pos + 4));
			break;
		case 0x3002: // container duration (4 or 8 bytes)
			// Kept apart from the structural-component durations: this one is
			// in the DESCRIPTOR's edit units (frames for video, samples for
			// audio), and the two pools only merge unit-aware in
			// parseHeaderMetadata's post-processing. Min-wins within the pool:
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
				out.bitDepth = MediaMetadataUtil::bitDepthLabel(readUint32BE(data, pos));
			break;
		case 0x3D01: // audio quantisation bits
			if (out.isAudio && len >= 4)
				out.bitDepth = MediaMetadataUtil::bitDepthLabel(readUint32BE(data, pos));
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
			if (out.isAudio && len >= 16 && out.compressionLabel.isEmpty())
				out.compressionLabel = data.mid(pos, len);
			break;
		}
		pos += len;
	}
}

void MxfParser::parsePackage(const QByteArray &data, qint64 startPos, qint64 length,
							 MediaMetadata &out, bool isMaterialPackage)
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
										 MediaMetadata &out)
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
			// units — parseHeaderMetadata resolves the units per kind (for audio
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
								 MediaMetadata &out)
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
	// parseHeaderMetadata resolves ownership after decoding individual candidates.
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
