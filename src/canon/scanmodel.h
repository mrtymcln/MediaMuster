#pragma once

// The shared in-memory records for a scan. Physical media files stay
// separate from the Avid objects and database records that describe them.
// Each file copy has its own row; source evidence can be shared between rows.

#include "mediaevidence.h"
#include <QByteArray>
#include <QStringList>
#include <atomic>
#include <optional>

namespace Canon
{
	enum class PmrFileSet
	{
		Legacy,
		Unicode
	};
	enum class TextEncoding
	{
		Ascii,
		MacRoman,
		Utf8,
		Utf16LE,
		Utf16BE,
		Unknown
	};

	/// Source-local handles preserve object contexts before identity reconciliation.
	using ObjectHandle = quint64;

	struct ObjectReference
	{
		// Use the owning ParsedSource's returned snapshot, not the reader's input receipt.
		SourceSnapshotRef source;
		ObjectHandle handle = 0;
	};

	struct ByteRange
	{
		qint64 offset = 0;
		qint64 length = 0;
	};

	struct PropertyLocator
	{
		QString name;
		QByteArray key; ///< Original tag/UL/dictionary key when supplied by the format.
		quint64 objectNumber = 0;
		QVector<ByteRange> ranges; ///< Empty when not located; multiple ranges retain fragmented values.
	};

	/// Unknown/private properties keep their encoding without invented semantics.
	struct RawProperty
	{
		PropertyLocator locator;
		QByteArray encoding;
		QVariant decoded;
		PropertyReadState state = PropertyReadState::NotRead;
		QString interpretation;							///< Decoding limits/encoding evidence, without display policy.
		std::optional<TextEncoding> textEncoding;		///< No value for binary/numeric/absent properties.
		std::optional<EvidenceBasis> textEncodingBasis; ///< No value when encoding is unknown.
		bool bytesRetained = true;						///< False: locator ranges reference bytes not copied into RAM.
	};

	struct AvidObject
	{
		enum class Role
		{
			Unknown,
			Master,
			FileSource,
			PhysicalSource,
			Composition,
			Descriptor,
			Track,
			Component
		};
		ObjectHandle handle = 0;
		Role role = Role::Unknown;
		SourceSnapshotRef snapshot;
		QByteArray recordedIdentity;
		QString identityEncoding; ///< Reader-established encoding, not an assumed byte order.
		QVector<RawProperty> properties;
	};

	struct Relationship
	{
		// Handles are local to the containing ParsedSource; zero is unresolved.
		ObjectHandle origin = 0;
		ObjectHandle target = 0;
		PropertyLocator locator;
		QVariant recordedReference;
		QString referenceEncoding;
		EvidenceBasis basis = EvidenceBasis::Recorded;
		QString explanation;
	};

	struct RecordSet
	{
		QString name;
		std::optional<PmrFileSet> pmrFileSet; ///< PMR only; other containers do not inherit these sets.
		qint32 version = 0;
		quint32 declaredCount = 0;
		QVector<ObjectHandle> objects; ///< Includes partial records, without positional merging.
		bool framingComplete = false;
	};

	/// Parsers return source-local facts and references; they never choose UI values.
	struct ParsedSource
	{
		enum class Container
		{
			Unknown,
			Pmr,
			Bento,
			Mxf,
			Omf,
			Wave,
			Aiff
		};
		enum class Outcome
		{
			NotRead,
			Complete,
			Incomplete,
			Malformed,
			Unsupported,
			IoError,
			Cancelled
		};
		Outcome outcome = Outcome::NotRead;
		SourceSnapshotRef snapshot;
		Container container = Container::Unknown;
		QVector<RecordSet> recordSets;
		QVector<AvidObject> objects;
		QVector<Relationship> relationships;
		QVector<RawProperty> unownedProperties;
		QStringList diagnostics;
	};

	/// Canonical physical record. UI strings belong in a later adapter.
	struct MediaFile
	{
		KelpieId kelpieId = 0;
		QString path;
		QString volumeIdentifier;
		qint64 sizeBytes = 0;
		QDateTime created;
		QDateTime modified;
		bool omfScan = false; ///< Accepted managed family, not actual container.
		bool quarantined = false;
		MediaEvidence evidence;
		MediaScanStamp stamp;
		QVector<ObjectReference> objects;
	};

	struct SourceCandidate
	{
		enum class ReaderHint
		{
			Pmr,
			Mdb,
			Mxf,
			LegacyMedia
		};
		ReaderHint hint;
		QString path;
		QDateTime modified;
		KelpieId kelpieId = 0; ///< Zero for databases; physical files have their row ID.
	};

	struct DiscoveryIssue
	{
		enum class Kind
		{
			UnmanagedRoot,
			UnavailableRoot,
			UnreadableFolder
		};
		Kind kind;
		QString path;
		QString explanation;
	};

	struct ScanRequest
	{
		/// Managed roots/leaves or their direct containing bases; no recursive search.
		QStringList roots;
		bool omfScan = true;
	};

	struct ScanResult
	{
		ScanRequest request;
		QVector<MediaFile> files;
		QVector<SourceCandidate> candidates;
		QVector<ParsedSource> sources;
		QVector<DiscoveryIssue> discoveryIssues;
		QVector<ScanIssue> reconciliationIssues;
		bool discoveryComplete = true;
		bool cancelled = false;
		// Discovery does not claim parsing/reconciliation has completed.
		bool parsingComplete = false;
		bool reconciliationComplete = false;
	};

	class Cancellation
	{
	public:
		void cancel() { m_cancelled.store(true); }
		bool cancelled() const { return m_cancelled.load(); }

	private:
		std::atomic_bool m_cancelled{false};
	};
}
