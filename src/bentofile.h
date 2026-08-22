#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QString>
#include <QVector>

// MARK: - BentoFile

/// A read-only view of an Apple Bento container — the 1990s OpenDoc format
/// that OMF Interchange adopted, and therefore the shape of Avid's
/// `msmMMOB.mdb` (the label also closes every `.avb` bin). Three parts:
///
///   label   the LAST 24 bytes: magic A4 43 4D A5 48 64 72 D7 | u16 flags |
///           'II' | u32 version | u32 tocOffset | u32 tocLength
///   TOC     tocLength/24 entries, each 24 bytes little-endian:
///           u32 objectID | u32 propertyID | u32 typeID | u32 value |
///           u32 length | u16 generation | u16 flags
///           flags bit 0 set = the value is IMMEDIATE: its bytes are the four
///           raw bytes of the `value` field, file order, truncated to length.
///           Clear = `value` is an offset into the value area [0, tocOffset).
///   values  everything before the TOC.
///
/// Names: entries with propertyID 24 carry a NUL-terminated property name
/// ("OMFI:MOBJ:MobID") whose objectID IS that property's id — the file
/// embeds its own dictionary, so nothing here is hard-coded. (propertyID 23
/// is the type-name dictionary; not needed.) Object class is the property
/// `OMFI:ObjID`, a 4-byte immediate FourCC ("MOBJ", "CDCI", "PCMA"...).
///
/// Verified against four real MDBs (53 KB – 6.4 MB): tocOffset + tocLength
/// == size − 24 on every one; objectIDs are NOT monotonic; the same
/// (object, property) can appear twice (MobID is written twice per object) —
/// the entries are stable-sorted so the FIRST in file order wins; two entries
/// per file (object 1, properties 4 and 5 — the TOC and the whole file) point
/// outside the value area and simply read as empty.
///
/// Holds a copy of the bytes; meant to live on the stack inside a parser, not
/// to be cached — a 5,000-file folder's MDB is ~40 MB.
class BentoFile
{
public:
	struct Entry
	{
		quint32 object = 0;
		quint32 property = 0;
		quint32 type = 0;
		quint32 value = 0;	///< Offset into the value area, or the immediate bytes.
		quint32 length = 0;
		quint32 tocPos = 0; ///< Byte offset of this entry's 24 bytes in the file.
		bool immediate = false;
	};

	/// Take a copy of `data` and index it. False (with a reason in `why`) on
	/// a missing/garbled label or TOC arithmetic that doesn't close — the
	/// caller treats that as "not a Bento file", never as a partial read.
	bool load(const QByteArray &data, QString *why = nullptr);

	// MARK: Lookup

	/// The id a property name ("OMFI:CPNT:Name") carries in THIS file, or -1.
	[[nodiscard]] int propertyId(QByteArrayView name) const;

	/// The first (file-order) value stored for `property` on `object`, or an
	/// empty view. Immediate values view the TOC's own bytes; offset values
	/// are bounds-clamped to the value area, so an out-of-range entry reads
	/// empty rather than crashing.
	[[nodiscard]] QByteArrayView value(quint32 object, int property) const;

	/// `OMFI:ObjID` as the 4 raw bytes ("MOBJ"), or empty.
	[[nodiscard]] QByteArray objectClass(quint32 object) const;

	/// Every object carrying `property`, ascending, each once.
	[[nodiscard]] QVector<quint32> objectsWithProperty(int property) const;

	// MARK: Typed readers over a value view (all little-endian)

	/// Unsigned integer of the view's width (1, 2 or 4 bytes occur). 0 if empty.
	[[nodiscard]] static quint32 uint(QByteArrayView v);
	/// OMF rational: i32 numerator, i32 denominator. False unless 8 bytes.
	static bool rational(QByteArrayView v, qint32 &num, qint32 &den);
	/// One object reference: u32 objectID | u32 0 (8 bytes). 0 if malformed.
	[[nodiscard]] static quint32 handle(QByteArrayView v);
	/// An object-reference array: u16 count, then count × 8-byte handles.
	[[nodiscard]] static QVector<quint32> handles(QByteArrayView v);
	/// NUL-terminated text, UTF-8 first then MacRoman (AvidText::decode).
	[[nodiscard]] static QString string(QByteArrayView v);
	/// NUL-terminated UTF-8 (the `...UTF8` twin properties).
	[[nodiscard]] static QString utf8String(QByteArrayView v);
	/// A 32-byte MobID rendered in MediaMuster's dotted hex (MobId::format).
	[[nodiscard]] static QString mobIdHex(QByteArrayView v);

	// MARK: Facts about the file (for logs and tests)

	[[nodiscard]] int entryCount() const { return m_entries.size(); }
	[[nodiscard]] int propertyNameCount() const { return m_propIdByName.size(); }
	[[nodiscard]] quint32 tocOffset() const { return m_tocOffset; }

private:
	[[nodiscard]] const Entry *find(quint32 object, quint32 property) const;

	QByteArray m_data;
	quint32 m_tocOffset = 0;
	QVector<Entry> m_entries; ///< Stable-sorted by (object, property).
	QHash<QByteArray, int> m_propIdByName;
	int m_objIdProperty = -1; ///< propertyId("OMFI:ObjID"), resolved once.
};
