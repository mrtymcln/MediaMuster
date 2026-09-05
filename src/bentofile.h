#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QHash>
#include <QString>
#include <QVector>

/// Read-only Bento 1 fixed-record and Bento 2 compact-TOC container reader.
/// The Bento revision describes the container, independently of OMF1/OMF2.
/// load() and open() expose the same values; open() fetches metadata on demand.
/// Instances own their returned views and are intended for one parser/thread.
class BentoFile
{
public:
	struct Entry
	{
		quint32 object = 0, property = 0, type = 0;
		quint64 value = 0, length = 0, tocPos = 0;
		bool immediate = false, continued = false;
		quint32 referenceList = 0;
		QByteArray immediateData;
		int nextSegment = -1; ///< Index after sorting; only a continued segment links onward.
	};
	static constexpr qint64 kMaxValueBytes = 1024 * 1024;
	enum class ReadStatus
	{
		Ok,
		Missing,
		Malformed,
		TooLarge,
		IoError,
		Unsupported
	};
	struct ReadResult
	{
		QByteArray data;
		ReadStatus status = ReadStatus::Missing;
		[[nodiscard]] bool ok() const { return status == ReadStatus::Ok; }
	};

	bool load(const QByteArray &data, QString *why = nullptr);
	bool open(const QString &path, QString *why = nullptr);
	[[nodiscard]] int propertyId(QByteArrayView name) const;
	[[nodiscard]] bool hasProperty(quint32 object, int property) const;
	/// Missing and unreadable are distinct. Never returns a truncated value.
	[[nodiscard]] ReadResult read(quint32 object, int property, qint64 cap = kMaxValueBytes) const;
	/// Compatibility adapters: use read() when absence changes interpretation.
	[[nodiscard]] QByteArrayView value(quint32 object, int property) const;
	[[nodiscard]] QByteArray bytes(quint32 object, int property, qint64 cap = kMaxValueBytes) const;
	[[nodiscard]] QByteArray objectClass(quint32 object) const;
	[[nodiscard]] QVector<quint32> objectsWithProperty(int property) const;

	/// Payload scalars use HEAD ByteOrder; container offsets have their own order.
	[[nodiscard]] bool isBigEndian() const { return m_metadataBigEndian; }
	[[nodiscard]] bool containerIsBigEndian() const { return m_containerBigEndian; }
	[[nodiscard]] quint16 containerVersion() const { return m_major; }
	void setMetadataBigEndian(bool big) { m_metadataBigEndian = big; }
	void setOmf2References(bool omf2) { m_omf2References = omf2; }
	[[nodiscard]] quint32 uintValue(QByteArrayView v) const;
	[[nodiscard]] quint64 uint64Value(QByteArrayView v) const;
	[[nodiscard]] qint64 int64Value(QByteArrayView v) const;
	bool rationalValue(QByteArrayView v, qint32 &num, qint32 &den) const;
	[[nodiscard]] quint32 handleValue(QByteArrayView v) const;
	[[nodiscard]] QVector<quint32> handlesValue(QByteArrayView v) const;
	/// Context-aware references also resolve Bento2 reference-list keys.
	[[nodiscard]] quint32 ref(quint32 object, int property, ReadStatus *status = nullptr) const;
	[[nodiscard]] QVector<quint32> refs(quint32 object, int property, ReadStatus *status = nullptr) const;

	/// Legacy little-endian helpers retained for callers constructing raw OMF1 data.
	[[nodiscard]] static quint32 uint(QByteArrayView v);
	static bool rational(QByteArrayView v, qint32 &num, qint32 &den);
	[[nodiscard]] static quint32 handle(QByteArrayView v);
	[[nodiscard]] static QVector<quint32> handles(QByteArrayView v);
	[[nodiscard]] static QString string(QByteArrayView v);
	[[nodiscard]] static QString utf8String(QByteArrayView v);
	[[nodiscard]] static QString mobIdHex(QByteArrayView v);
	struct MobIndexEntry
	{
		QByteArray uid;
		quint32 object = 0;
	};
	[[nodiscard]] static QVector<MobIndexEntry> mobIndex(QByteArrayView v);

	[[nodiscard]] int entryCount() const { return m_entries.size(); }
	[[nodiscard]] int propertyNameCount() const { return m_propIdByName.size(); }
	[[nodiscard]] quint64 tocOffset() const { return m_tocOffset; }
	[[nodiscard]] const QVector<Entry> &entries() const { return m_entries; }
	[[nodiscard]] qint64 bytesRead() const { return m_bytesRead; }

private:
	[[nodiscard]] const Entry *find(quint32 object, quint32 property) const;
	bool checkLabel(QByteArrayView label, qint64 fileSize, QString &reason);
	bool locateLabel(qint64 fileSize, QByteArray &label, quint64 &labelEnd, QString &reason);
	bool indexToc(QByteArrayView toc, QString &reason);
	bool indexNames(QString &reason);
	bool fetch(quint64 at, quint64 length, QByteArray &out) const;
	[[nodiscard]] quint32 mappedReference(const Entry &entry, QByteArrayView raw, ReadStatus &status) const;
	void reset();

	QByteArray m_data, m_toc, m_dict;
	quint64 m_tocOffset = 0, m_tocLength = 0, m_dictOffset = 0;
	quint32 m_tocBlockSize = 0;
	quint16 m_major = 0;
	bool m_containerBigEndian = false, m_metadataBigEndian = false, m_omf2References = false;
	bool m_tailFirst = false;
	QVector<Entry> m_entries;
	QHash<QByteArray, int> m_propIdByName;
	int m_objIdProperty = -1, m_objClassProperty = -1;
	mutable QHash<quint64, QByteArray> m_views;
	mutable QFile m_file;
	mutable qint64 m_bytesRead = 0;
};
