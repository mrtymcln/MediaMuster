#pragma once

// Diagnostic serialization of semantic records, never a production file format.
// Lists retain their recorded order; only unordered map keys are sorted. Pointer
// IDs describe alias topology without depending on allocator addresses.

#include "canon/avbreferences.h"
#include "canon/projection.h"
#include "canon/scanengine.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QTimeZone>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Canon2Proof
{

class HashSink final : public QIODevice
{
public:
	HashSink()
	{
		m_buffer.reserve(65536);
		open(QIODevice::WriteOnly | QIODevice::Unbuffered);
	}
	QByteArray result()
	{
		flush();
		return m_hash.result().toHex();
	}
	qint64 serializedBytes = 0;

protected:
	qint64 readData(char *, qint64) override { return -1; }
	qint64 writeData(const char *data, qint64 size) override
	{
		const auto requested = size;
		serializedBytes += size;
		while (size > 0)
		{
			const auto count = qMin<qint64>(size, 65536 - m_buffer.size());
			m_buffer.append(data, count);
			data += count;
			size -= count;
			if (m_buffer.size() == 65536)
				flush();
		}
		return requested;
	}

private:
	void flush()
	{
		if (!m_buffer.isEmpty())
		{
			m_hash.addData(m_buffer);
			m_buffer.clear();
		}
	}
	QCryptographicHash m_hash{QCryptographicHash::Sha256};
	QByteArray m_buffer;
};

class SnapshotIds
{
public:
	quint64 id(const SourceSnapshotRef &snapshot)
	{
		if (!snapshot)
			return 0;
		const auto found = m_ids.constFind(snapshot.data());
		if (found != m_ids.cend())
			return found.value();
		const auto result = quint64(m_owners.size()) + 1;
		m_owners.append(snapshot); // Keep keys alive even after a scoped graph dies.
		m_ids.insert(snapshot.data(), result);
		return result;
	}
	void seed(const Canon::ScanResult &scan)
	{
		for (const auto &source : scan.sources)
			id(source.snapshot);
	}
	qsizetype size() const { return m_owners.size(); }

private:
	QHash<const SourceSnapshot *, quint64> m_ids;
	QVector<SourceSnapshotRef> m_owners;
};

class Fingerprint
{
public:
	explicit Fingerprint(SnapshotIds *snapshots = nullptr)
		: stream(&sink), m_snapshots(snapshots ? snapshots : &m_localSnapshots)
	{
		stream.setVersion(QDataStream::Qt_6_0);
		stream.setByteOrder(QDataStream::BigEndian);
		// Doubles in values are handled as integer bits below, never promoted floats.
	}

	HashSink sink;
	QDataStream stream;
	qint64 objects = 0;
	qint64 relationships = 0;
	qint64 properties = 0;
	qint64 encodingBytes = 0;

	void text(const QString &value) { stream << value.isNull() << value; }
	void bytes(const QByteArray &value) { stream << value.isNull() << value; }
	void texts(const QStringList &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
			text(value);
	}
	void dateTime(const QDateTime &value)
	{
		stream << value.isValid() << value.isNull() << value;
	}
	template <typename T> void optional(const std::optional<T> &value)
	{
		stream << bool(value);
		if (value)
			stream << qint32(*value);
	}

	void variant(const QVariant &value)
	{
		const auto type = value.metaType();
		stream << qint32(type.id());
		bytes(type.name() ? QByteArray(type.name()) : QByteArray{});
		stream << value.isValid() << value.isNull();
		if (!value.isValid())
			return;
		switch (type.id())
		{
		case QMetaType::Bool: stream << value.value<bool>(); break;
		case QMetaType::Char: stream << qint8(value.value<char>()); break;
		case QMetaType::SChar: stream << qint8(value.value<signed char>()); break;
		case QMetaType::UChar: stream << quint8(value.value<unsigned char>()); break;
		case QMetaType::Short: stream << qint16(value.value<short>()); break;
		case QMetaType::UShort: stream << quint16(value.value<unsigned short>()); break;
		case QMetaType::Int: stream << qint32(value.value<int>()); break;
		case QMetaType::UInt: stream << quint32(value.value<unsigned int>()); break;
		case QMetaType::Long: stream << qint64(value.value<long>()); break;
		case QMetaType::ULong: stream << quint64(value.value<unsigned long>()); break;
		case QMetaType::LongLong: stream << value.value<qint64>(); break;
		case QMetaType::ULongLong: stream << value.value<quint64>(); break;
		case QMetaType::Float:
		{
			quint32 bits = 0;
			std::memcpy(&bits, value.constData(), sizeof(bits));
			stream << bits;
			break;
		}
		case QMetaType::Double:
		{
			quint64 bits = 0;
			std::memcpy(&bits, value.constData(), sizeof(bits));
			stream << bits;
			break;
		}
		case QMetaType::QChar: stream << value.value<QChar>().unicode(); break;
		case QMetaType::QString: text(value.toString()); break;
		case QMetaType::QByteArray: bytes(value.toByteArray()); break;
		case QMetaType::QStringList: texts(value.toStringList()); break;
		case QMetaType::QDateTime: dateTime(value.toDateTime()); break;
		case QMetaType::QDate:
		{
			const auto date = value.toDate();
			stream << date.isValid() << date.isNull() << date;
			break;
		}
		case QMetaType::QTime:
		{
			const auto time = value.toTime();
			stream << time.isValid() << time.isNull() << time;
			break;
		}
		case QMetaType::QVariantList:
		{
			const auto values = value.toList();
			stream << qint64(values.size());
			for (const auto &item : values)
				variant(item);
			break;
		}
		case QMetaType::QVariantMap:
		{
			const auto values = value.toMap();
			stream << qint64(values.size());
			for (auto item = values.cbegin(); item != values.cend(); ++item)
			{
				text(item.key());
				variant(item.value());
			}
			break;
		}
		case QMetaType::QVariantHash:
		{
			const auto values = value.toHash();
			auto keys = values.keys();
			std::sort(keys.begin(), keys.end());
			stream << qint64(keys.size());
			for (const auto &key : keys)
			{
				text(key);
				variant(values.value(key));
			}
			break;
		}
		default:
			throw std::runtime_error(QStringLiteral("Unverified QVariant metatype %1 (%2)")
				.arg(type.id()).arg(QString::fromLatin1(type.name() ? type.name() : "unknown")).toStdString());
		}
	}

	void snapshot(const SourceSnapshotRef &value)
	{
		stream << m_snapshots->id(value);
		if (value)
		{
			stream << qint32(value->source);
			text(value->path);
			dateTime(value->modified);
			stream << qint32(value->readState);
		}
	}
	void range(const Canon::ByteRange &value) { stream << value.offset << value.length; }
	void ranges(const QVector<Canon::ByteRange> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
			range(value);
	}
	void locator(const Canon::PropertyLocator &value)
	{
		text(value.name);
		bytes(value.key);
		stream << value.objectNumber;
		ranges(value.ranges);
	}

	void property(const Canon::RawProperty &value)
	{
		++properties;
		encodingBytes += value.encoding.size();
		locator(value.locator);
		bytes(value.encoding);
		variant(value.decoded);
		stream << qint32(value.state);
		text(value.interpretation);
		optional(value.textEncoding);
		optional(value.textEncodingBasis);
		stream << contextId(value.bento.data());
		if (value.bento)
		{
			const auto &native = *value.bento;
			stream << native.property << native.type << native.generation << native.referenceListObject;
			text(native.typeName);
			ranges(native.tocRanges);
			optional(native.metadataBigEndian);
		}
		stream << contextId(value.mxf.data());
		if (value.mxf)
		{
			const auto &native = *value.mxf;
			stream << native.localTag << native.primerOffset;
			bytes(native.mappedAuid);
			text(native.typeName);
			bytes(native.framingBytes);
			ranges(native.framingRanges);
		}
		stream << value.bytesRetained;
	}
	void propertyList(const QVector<Canon::RawProperty> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
			property(value);
	}
	void graph(const Canon::ParsedSource &value)
	{
		stream << qint32(value.outcome);
		text(value.readReason);
		snapshot(value.snapshot);
		stream << qint32(value.container);
		optional(value.omfRevision);
		locator(value.embedding);
		stream << qint64(value.recordSets.size());
		for (const auto &set : value.recordSets)
		{
			text(set.name);
			optional(set.pmrFileSet);
			stream << set.version << set.declaredCount << qint64(set.objects.size());
			for (const auto object : set.objects)
				stream << object;
			stream << set.framingComplete;
		}
		stream << qint64(value.objects.size());
		objects += value.objects.size();
		for (const auto &object : value.objects)
		{
			stream << object.handle << qint32(object.role);
			snapshot(object.snapshot);
			bytes(object.recordedIdentity);
			text(object.identityEncoding);
			stream << contextId(object.mxf.data());
			if (object.mxf)
			{
				const auto &native = *object.mxf;
				bytes(native.key);
				text(native.name);
				stream << native.partitionOffset;
				range(native.framing);
				range(native.value);
			}
			stream << contextId(object.avb.data());
			if (object.avb)
			{
				const auto &native = *object.avb;
				bytes(native.classId);
				range(native.framing);
				range(native.value);
				stream << native.bigEndian << native.interpretationComplete;
			}
			propertyList(object.properties);
		}
		stream << qint64(value.relationships.size());
		relationships += value.relationships.size();
		for (const auto &link : value.relationships)
		{
			stream << link.origin << link.target;
			locator(link.locator);
			variant(link.recordedReference);
			text(link.referenceEncoding);
			stream << qint32(link.basis);
			text(link.explanation);
		}
		propertyList(value.unownedProperties);
		texts(value.diagnostics);
		stream << qint64(value.embeddedSources.size());
		for (const auto &child : value.embeddedSources)
			graph(child);
	}

	void readResult(const PropertyReadResult &value)
	{
		stream << qint32(value.state) << qint32(value.reason) << qint32(value.applicability);
		text(value.explanation);
	}
	void evidence(const MediaEvidence &value)
	{
		const auto &coverage = value.sourceCoverage();
		stream << qint64(coverage.size());
		for (const auto &item : coverage)
		{
			snapshot(item.snapshot);
			text(item.objectIdentity);
			stream << qint32(item.defaultReason) << item.eligible << qint32(item.freshness);
			auto keys = item.fields.keys();
			std::sort(keys.begin(), keys.end(), [](auto a, auto b) { return int(a) < int(b); });
			stream << qint64(keys.size());
			for (const auto key : keys)
			{
				stream << qint32(key);
				readResult(item.fields.value(key));
			}
			// Also verify derived read status for every coverage context and field.
			for (int index = 0; index < int(MediaProperty::Count); ++index)
			{
				readResult(value.readStatus(MediaProperty(index), item.snapshot, item.objectIdentity));
				readResult(value.readStatus(MediaProperty(index), item.snapshot, item.objectIdentity, true));
			}
		}
		stream << qint32(MediaProperty::Count);
		for (int index = 0; index < int(MediaProperty::Count); ++index)
		{
			const auto field = MediaProperty(index);
			const auto &observations = value.observations(field);
			stream << qint32(field) << qint64(observations.size());
			for (const auto &item : observations)
			{
				snapshot(item.snapshot);
				text(item.property);
				text(item.objectIdentity);
				variant(item.value);
				variant(item.rawValue);
				stream << qint32(item.readState) << qint32(item.readReason) << qint32(item.basis)
					   << qint32(item.freshness) << item.eligible;
				text(item.explanation);
				optional(item.textEncoding);
				optional(item.textEncodingBasis);
			}
			readResult(value.readStatus(field));
			readResult(value.readStatus(field, {}, {}, true));
			const auto selected = value.selected(field);
			variant(selected.value);
			stream << qint32(selected.readState) << qint32(selected.agreement) << selected.selectedObservation;
			text(selected.rule);
			text(selected.reason);
			stream << qint32(selected.readReason) << qint32(selected.applicability);
		}
	}
	void objectReferences(const QVector<Canon::ObjectReference> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
		{
			snapshot(value.source);
			stream << value.handle;
		}
	}
	void file(const Canon::MediaFile &value)
	{
		stream << value.kelpieId;
		text(value.path);
		text(value.volumeIdentifier);
		stream << value.sizeBytes;
		dateTime(value.created);
		dateTime(value.modified);
		stream << value.omfScan << value.quarantined;
		evidence(value.evidence);
		text(value.stamp.path);
		text(value.stamp.volumeIdentifier);
		dateTime(value.stamp.modified);
		text(value.stamp.mobId);
		texts(value.stamp.masterMobIds);
		objectReferences(value.objects);
	}
	void candidate(const Canon::SourceCandidate &value)
	{
		stream << qint32(value.hint);
		text(value.path);
		dateTime(value.modified);
		stream << value.kelpieId;
	}
	void storedReceipt(const Canon::StoredSource &value)
	{
		stream << qint32(value.outcome) << qint32(value.container);
		text(value.readReason);
		snapshot(value.snapshot);
		texts(value.diagnostics);
	}
	void projection(const Canon::Projection &value)
	{
		projectedFiles(value.files);
		projectedFiles(value.masters);
		texts(value.diagnostics);
	}
	void avb(const Canon::ParsedSource &source, const Canon::Cancellation &cancellation)
	{
		const auto owned = QSharedPointer<Canon::ParsedSource>::create(source);
		const Canon::AvbReferenceIndex index({owned}, cancellation);
		stream << qint64(index.sequences().size());
		for (const auto &sequence : index.sequences())
		{
			avbKey(sequence.key);
			text(sequence.name);
			bytes(sequence.mobId);
			stream << sequence.userPlaced;
			locator(sequence.membership);
		}
		avbResolution(index.resolve({{0, Canon::AvbScope::Kind::EntireBin, {}}}, cancellation));
		for (const auto &sequence : index.sequences())
			avbResolution(index.resolve({{0, Canon::AvbScope::Kind::SelectedSequences, {sequence.key.object}}}, cancellation));
	}
	QByteArray result()
	{
		if (stream.status() != QDataStream::Ok)
			throw std::runtime_error("Fingerprint serialization failed");
		return sink.result();
	}

private:
	quint64 contextId(const void *context)
	{
		if (!context)
			return 0;
		const auto found = m_contexts.constFind(context);
		if (found != m_contexts.cend())
			return found.value();
		const auto result = quint64(m_contexts.size()) + 1;
		m_contexts.insert(context, result);
		return result;
	}
	void projectedFiles(const QVector<Canon::ProjectedFile> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
		{
			text(value.fileMobId);
			texts(value.masterMobIds);
			texts(value.filenames);
			objectReferences(value.objects);
			auto selected = value.evidence;
			Canon::selectMetadata(selected);
			evidence(selected);
		}
	}
	void avbKey(const Canon::AvbObjectKey &value) { stream << qint64(value.source) << value.object; }
	void avbResolution(const Canon::AvbResolution &value)
	{
		stream << value.complete << value.cancelled << qint64(value.roots.size());
		for (const auto &root : value.roots)
			avbKey(root);
		stream << qint64(value.edges.size());
		for (const auto &edge : value.edges)
		{
			avbKey(edge.origin);
			avbKey(edge.target);
			stream << qint64(edge.relationship);
		}
		stream << qint64(value.media.size());
		for (const auto &media : value.media)
		{
			avbKey(media.locator);
			stream << qint64(media.property);
			bytes(media.mobId);
			bytes(media.legacyId);
		}
		stream << qint64(value.terminals.size());
		for (const auto &terminal : value.terminals)
		{
			avbKey(terminal.object);
			stream << qint64(terminal.relationship) << qint32(terminal.kind);
		}
		stream << qint64(value.issues.size());
		for (const auto &issue : value.issues)
		{
			stream << qint32(issue.kind);
			avbKey(issue.object);
			locator(issue.property);
			text(issue.explanation);
		}
	}
	SnapshotIds m_localSnapshots;
	SnapshotIds *m_snapshots;
	// One Fingerprint per source graph keeps this table scoped to that tree.
	QHash<const void *, quint64> m_contexts;
};

inline Canon::Projection project(const Canon::ParsedSource &source, const Canon::Cancellation &cancellation)
{
	if (source.container == Canon::ParsedSource::Container::Pmr)
		return Canon::projectPmr(source, cancellation);
	if (source.container == Canon::ParsedSource::Container::Mxf)
		return Canon::projectMxf(source, cancellation);
	if (source.snapshot && source.snapshot->source == MetadataSource::Mdb)
		return Canon::projectMdb(source, cancellation);
	return Canon::projectOmf(source, cancellation);
}

} // namespace Canon2Proof
