#pragma once

#include <QDateTime>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <limits>
#include <stdexcept>

// MARK: - Scan identifiers
using KelpieId = quint64;

class KelpieIdExhausted : public std::runtime_error
{
public:
	KelpieIdExhausted() : std::runtime_error("Scan-session KelpieIds exhausted") {}
};

/// Owned by the scan/session coordinator, never by a folder worker.
class KelpieIdAllocator
{
public:
	KelpieId allocate()
	{
		if (m_next == 0)
			throw KelpieIdExhausted{};
		const KelpieId id = m_next;
		m_next = id == std::numeric_limits<KelpieId>::max() ? 0 : id + 1;
		return id;
	}
	void reserveThrough(KelpieId id)
	{
		if (m_next != 0 && id >= m_next)
			m_next = id == std::numeric_limits<KelpieId>::max() ? 0 : id + 1;
	}
	void reset() { m_next = 1; }

private:
	KelpieId m_next = 1;
};

// MARK: - Read states and metadata sources
enum class PropertyReadState : quint8
{
	NotRead,   ///< No interpreted read result established; this does not prove absence.
	Present,   ///< A value was read, including empty text, false or zero; it need not be selected.
	Absent,	   ///< A checked object or established format layout does not supply the property.
	Unreadable ///< The recognized value/source could not be read or decoded; raw bytes may remain.
};

enum class EvidenceBasis : quint8
{
	Recorded, ///< The source explicitly supplied the value.
	Derived	  ///< Calculated or inferred from evidence, with an explanation.
};
enum class MetadataSource : quint8
{
	Filesystem,
	Pmr,
	Mdb,
	Mxf,
	Omf,
	Avb
};
enum class SourceReadState : quint8
{
	NotRead,
	Complete,
	Incomplete,
	Unreadable
};

// Shared application classification, independent of the presentation record.
enum class MediaType : int
{
	Unknown = -1,
	Media = 0,
	Precompute = 1
};

/// Shared immutable receipt for a source-local object graph.
// MARK: - Source snapshots and scan stamps
struct SourceSnapshot
{
	MetadataSource source = MetadataSource::Filesystem;
	QString path;
	QDateTime modified;
	SourceReadState readState = SourceReadState::NotRead;
};
using SourceSnapshotRef = QSharedPointer<const SourceSnapshot>;

/// The agreed five-field scan receipt; neither file-object identity nor byte proof.
struct MediaScanStamp
{
	QString path;
	QString volumeIdentifier;
	QDateTime modified;
	QString fileMobId;
	QString masterMobId;
};

// MARK: - OpHeaderIdentity

/// Captured header identities to recheck independently of the selected MasterMobId.
/// Bella supplies its qualified FileMobId; saved jobs can also require header master IDs.
/// Database-only master associations are not requirements imposed on the header.
struct OperationHeaderIdentity
{
	QString fileMobId;
	QStringList masterMobIds;
	QString unavailableReason; ///< A changed or contradictory header cannot authorize an operation.
};

// MARK: - OpScanReceipt

/// The scan's safety conclusions and captured identities required for operations.
/// Owns no evidence, parsed source or scan session.
struct OperationScanReceipt
{
	OperationHeaderIdentity headerIdentity;
	QString databaseFileMobIdToVerify;
};
