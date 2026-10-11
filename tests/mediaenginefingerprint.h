#pragma once

// Diagnostic serialization of semantic records, never a production file format.
// Lists retain their recorded order; only unordered map keys are sorted. Pointer
// IDs describe alias topology without depending on allocator addresses.

#include "mediaengine/projection.h"
#include "mediaengine/scancoordinator.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace MediaEngineProof
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
		m_owners.append(snapshot); // Keep receipt aliases valid throughout the proof.
		m_ids.insert(snapshot.data(), result);
		return result;
	}
	void seed(const MediaEngine::ScanResult &scan)
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
	void readResult(const PropertyReadResult &value)
	{
		stream << qint32(value.state) << qint32(value.reason) << qint32(value.applicability);
		text(value.explanation.text());
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
			text(selected.reason.text());
			stream << qint32(selected.readReason) << qint32(selected.applicability);
		}
	}
	void objectReferences(const QVector<MediaEngine::ObjectReference> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
		{
			snapshot(value.source);
			stream << value.handle;
		}
	}
	void file(const MediaEngine::MediaFile &value)
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
	void candidate(const MediaEngine::SourceCandidate &value)
	{
		stream << qint32(value.hint);
		text(value.path);
		dateTime(value.modified);
		stream << value.kelpieId;
	}
	void sourceReceipt(const MediaEngine::SourceReceipt &value)
	{
		stream << qint32(value.outcome) << qint32(value.container);
		text(value.readReason);
		snapshot(value.snapshot);
		texts(value.diagnostics);
	}
	void projection(const MediaEngine::Projection &value)
	{
		projectedFiles(value.files);
		projectedFiles(value.masters);
		texts(value.diagnostics);
	}
	QByteArray result()
	{
		if (stream.status() != QDataStream::Ok)
			throw std::runtime_error("Fingerprint serialization failed");
		return sink.result();
	}

private:
	void projectedFiles(const QVector<MediaEngine::ProjectedFile> &values)
	{
		stream << qint64(values.size());
		for (const auto &value : values)
		{
			text(value.fileMobId);
			texts(value.masterMobIds);
			texts(value.filenames);
			objectReferences(value.objects);
			auto selected = value.evidence;
			MediaEngine::selectMetadata(selected);
			evidence(selected);
		}
	}
	SnapshotIds m_localSnapshots;
	SnapshotIds *m_snapshots;
};

} // namespace MediaEngineProof
