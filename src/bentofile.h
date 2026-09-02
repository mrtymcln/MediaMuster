#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QHash>
#include <QString>
#include <QVector>

// MARK: - BentoFile

/// A read-only view of an Apple Bento container — the 1990s OpenDoc format
/// that OMF Interchange adopted, and therefore the shape of Avid's
/// `msmMMOB.mdb`. A `.avb` bin and an `msmFMID.pmr` are each their own flat
/// format and carry no label (checked byte by byte across 66 bins and 3
/// PMRs). Three parts:
///
///   label   the LAST 24 bytes: magic A4 43 4D A5 48 64 72 D7 | u16 flags |
///           'II' | u16 majorVersion | u16 minorVersion | u32 tocOffset |
///           u32 tocLength
///   TOC     tocLength/24 entries, each 24 bytes little-endian:
///           u32 objectID | u32 propertyID | u32 typeID | u32 value |
///           u32 length | u16 generation | u16 flags
///           flags bit 0 set = the value is IMMEDIATE: its bytes are the four
///           raw bytes of the `value` field, file order, truncated to length.
///           Clear = `value` is an offset into the value area [0, tocOffset).
///           flags bit 1 set = a CONTINUED value (spread over several
///           entries) — never seen. `load()` keeps the entry and reads it
///           as EMPTY (one warning per file), so an MXF-era database with
///           one such value still loads as it always has; `open()` refuses
///           the file, since a tail-first reader cannot assemble the parts.
///   values  everything before the TOC.
///
/// Names: entries with propertyID 24 carry a NUL-terminated property name
/// ("OMFI:MOBJ:MobID") whose objectID IS that property's id — the file
/// embeds its own dictionary, so nothing here is hard-coded. (propertyID 23
/// is the type-name dictionary; not needed.) Object class is the property
/// `OMFI:ObjID`, a 4-byte immediate FourCC ("MOBJ", "CDCI", "PCMA"...).
///
/// Verified against four real MDBs (53 KB – 6.4 MB): tocOffset + tocLength
/// == size − 24 on every one; label version is 1.0; objectIDs are NOT
/// monotonic; the same (object, property) can appear twice (MobID is written
/// twice per object) — the entries are stable-sorted so the FIRST in file
/// order wins; two entries per file (object 1, properties 4 and 5 — the TOC
/// and the whole file) point outside the value area and simply read as empty.
///
/// OMF-era: every OMF essence file (.omf / .wav / .aif written by Media
/// Composer) is this same container — essence first at offset 0, the
/// metadata TOC at the tail — so the reader has two modes. `load()` copies
/// the whole buffer (right for a database of a few megabytes, and what every
/// MDB path uses). `open()` reads only the label, the TOC and the property
/// dictionary span that sits just below it (~20 KB regardless of file size;
/// verified on all 88 specimens) and serves values by seek+read on demand,
/// so a multi-gigabyte picture is never materialised. `value()` (a view) is
/// load-mode only; `bytes()` works in both, which is why every OMF-era reader
/// uses it.
///
/// Meant to live on the stack inside a parser, not to be cached — a
/// 5,000-file folder's MDB is ~40 MB. One instance serves one thread: the
/// tail-first mode seeks a shared file handle.
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
		bool continued = false; ///< Flag bit 1: the value spans more entries; reads empty.
	};

	/// Largest value `bytes()` will materialise by default (1 MiB). Every
	/// metadata value is a few KB at most; the only bigger ones are an OMF
	/// file's essence blob and the container's self-describing "whole file"
	/// entry, both of which must read as empty rather than be copied.
	static constexpr qint64 kMaxValueBytes = 1024 * 1024;

	/// Take a copy of `data` and index it. False (with a reason in `why`) on
	/// a missing/garbled label or TOC arithmetic that doesn't close — the
	/// caller treats that as "not a Bento file", never as a partial read.
	/// A label major version other than 1 and a continued entry are each
	/// logged and tolerated here (the MXF-era MDB reader has parsed
	/// unversioned files this way for months); only `open()` refuses them.
	bool load(const QByteArray &data, QString *why = nullptr);

	/// OMF-era: tail-first mode. Reads the label, the TOC and the property
	/// dictionary span from `path` and keeps the file open so `bytes()` can
	/// fetch values on demand. Same gates and reasons as `load()`, plus
	/// "cannot open", an unsupported label major version, and a continued
	/// entry (a partial value cannot be assembled from a seek+read reader,
	/// so the file is refused whole rather than read short); on failure
	/// `bytesRead()` still says how far it got.
	bool open(const QString &path, QString *why = nullptr);

	// MARK: Lookup

	/// The id a property name ("OMFI:CPNT:Name") carries in THIS file, or -1.
	[[nodiscard]] int propertyId(QByteArrayView name) const;

	/// The first (file-order) value stored for `property` on `object`, or an
	/// empty view. Immediate values view the TOC's own bytes; offset values
	/// are bounds-clamped to the value area, so an out-of-range entry reads
	/// empty rather than crashing, and a continued entry reads empty rather
	/// than truncated. Load mode only: after `open()` there is no buffer to
	/// view, so this reads empty — use `bytes()`.
	[[nodiscard]] QByteArrayView value(quint32 object, int property) const;

	/// The same value as an owned copy, in either mode. Empty for a missing
	/// entry, a continued entry, an out-of-range offset, or a value longer
	/// than `cap` — the cap is what keeps an OMF file's essence entry from
	/// ever being read.
	[[nodiscard]] QByteArray bytes(quint32 object, int property, qint64 cap = kMaxValueBytes) const;

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

	/// OMF-era: one row of a HEAD object's mob index (`OMFI:SourceMobs`,
	/// `OMFI:CompositionMobs`, `OMFI:MediaData`).
	struct MobIndexEntry
	{
		QByteArray uid; ///< The 12-byte omfi:UID (see OmfUid::canonicalHex).
		quint32 object = 0;
	};
	/// OMF-era: a mob index — u16 count, then count × 20 bytes: 12-byte UID
	/// | u32 objectID | u32 junk. `handles()` reads this shape as nothing
	/// (its trailing word is not always zero), hence a reader of its own.
	/// Rows with a zero objectID are dropped; malformed input reads empty.
	[[nodiscard]] static QVector<MobIndexEntry> mobIndex(QByteArrayView v);

	// MARK: Facts about the file (for logs and tests)

	[[nodiscard]] int entryCount() const { return m_entries.size(); }
	[[nodiscard]] int propertyNameCount() const { return m_propIdByName.size(); }
	[[nodiscard]] quint32 tocOffset() const { return m_tocOffset; }
	/// Every TOC entry, stable-sorted by (object, property), in either mode.
	[[nodiscard]] const QVector<Entry> &entries() const { return m_entries; }
	/// Bytes fetched from disk so far: the whole buffer after `load()`;
	/// label + TOC + dictionary span + every `bytes()` fetch after `open()`.
	/// Kept across a failed `open()` so a caller can see where it stopped.
	[[nodiscard]] qint64 bytesRead() const { return m_bytesRead; }

private:
	[[nodiscard]] const Entry *find(quint32 object, quint32 property) const;

	/// The label gates shared by both modes: magic, byte order, TOC
	/// arithmetic that closes on `fileSize`, TOC length a multiple of 24.
	/// The major version is handed back, not judged: `open()` refuses
	/// anything but 1, `load()` only warns (a minor bump changes nothing
	/// about the TOC layout, and the MXF-era reader never checked at all).
	static bool checkLabel(const char *label, qint64 fileSize, quint32 &tocOff, quint32 &tocLen, quint16 &major,
						   QString &reason);
	/// Parse `tocLen` bytes of TOC at `toc` (which sits at `tocOff` in the
	/// file) into `m_entries`, sorted. A continued-value entry is refused
	/// (false, with a reason) when `refuseContinued`, else kept with its
	/// `continued` flag set; `continuedCount` receives how many there were.
	bool indexToc(const char *toc, quint32 tocOff, quint32 tocLen, bool refuseContinued, int &continuedCount,
				  QString &reason);
	/// Build the name dictionary from the property-name entries whose bytes
	/// lie inside the `baseLen` bytes at `base`, which start at file offset
	/// `baseOffset` (the whole value area in load mode, the span in open mode).
	void indexNames(const char *base, quint32 baseOffset, qint64 baseLen);
	void reset();

	QByteArray m_data; ///< Load mode: the whole file.
	quint32 m_tocOffset = 0;
	QVector<Entry> m_entries; ///< Stable-sorted by (object, property).
	QHash<QByteArray, int> m_propIdByName;
	int m_objIdProperty = -1; ///< propertyId("OMFI:ObjID"), resolved once.

	// OMF-era: tail-first state. The TOC and the dictionary span are held in
	// memory; everything else is fetched through the open file.
	bool m_tailFirst = false;
	QByteArray m_toc;		///< The TOC bytes, file offset m_tocOffset.
	QByteArray m_dict;		///< The property/type-name span, file offset m_dictOffset.
	quint32 m_dictOffset = 0;
	mutable QFile m_file;	///< Seeked from const `bytes()`; one thread per instance.
	mutable qint64 m_bytesRead = 0;
};
