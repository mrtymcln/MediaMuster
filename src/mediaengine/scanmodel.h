#pragma once

// The shared in-memory records for a scan. Physical media files stay
// separate from the Avid objects and database records that describe them.
// Each file copy has its own row; source evidence can be shared between rows.

#include "mediaevidence.h"
#include <QByteArray>
#include <QStringList>
#include <atomic>
#include <optional>

namespace MediaEngine
{
	class Cancellation;
	class SourceArchive;
	class SourceStore;
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

	enum class OmfRevision
	{
		V1 = 1,
		V2 = 2
	};

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
		QByteArray key; ///< Source tag/UL/key; Bento numeric IDs use four big-endian bytes (native framing retained separately).
		quint64 objectNumber = 0;
		QVector<ByteRange> ranges; ///< Empty when not located; multiple ranges retain fragmented values.
	};

	// MDB/OMF values have a native type and TOC framing as well as a property name.
	// Keep that source context even when its private meaning is not yet understood.
	struct BentoPropertyContext
	{
		quint32 property = 0;
		quint32 type = 0;
		quint32 generation = 0;
		quint32 referenceListObject = 0;
		QString typeName;
		QVector<ByteRange> tocRanges;
		std::optional<bool> metadataBigEndian;
	};

	// MXF local tags only acquire meaning through their own Primer Pack. Keep
	// that mapping and framing separate from the property's original value bytes.
	struct MxfPropertyContext
	{
		quint32 localTag = 0;
		qint64 primerOffset = -1;
		QByteArray mappedAuid;
		QString typeName;
		QByteArray framingBytes; ///< Original local tag and length, including BER spelling.
		QVector<ByteRange> framingRanges;
	};

	struct MxfSetContext
	{
		QByteArray key;
		QString name;
		qint64 partitionOffset = -1;
		ByteRange framing;
		ByteRange value;
	};

	// AVB fields are positional inside a typed chunk. Preserve the chunk's
	// identity and byte order even when a newer field layout is not understood.
	struct AvbObjectContext
	{
		QByteArray classId;
		ByteRange framing;
		ByteRange value;
		bool bigEndian = false;
		bool interpretationComplete = false;
	};

	/// Unknown/private properties keep their encoding without invented semantics.
	struct RawProperty
	{
		PropertyLocator locator;
		QByteArray encoding;
		QVariant decoded;
		PropertyReadState state = PropertyReadState::NotRead;
		QString interpretation;							  ///< Decoding limits/encoding evidence, without display policy.
		std::optional<TextEncoding> textEncoding;		  ///< No value for binary/numeric/absent properties.
		std::optional<EvidenceBasis> textEncodingBasis;	  ///< No value when encoding is unknown.
		QSharedPointer<const BentoPropertyContext> bento; ///< Null for formats without Bento framing.
		QSharedPointer<const MxfPropertyContext> mxf;	  ///< Null outside MXF local sets.
		bool bytesRetained = true;						  ///< False: locator ranges reference bytes not copied into RAM.
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
		QString identityEncoding;				 ///< Reader-established encoding, not an assumed byte order.
		QSharedPointer<const MxfSetContext> mxf; ///< MXF set/partition context, including repeated metadata copies.
		QSharedPointer<const AvbObjectContext> avb;
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
			Aiff,
			Avb
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
		QString readReason; ///< Scan scheduling decision; empty for direct reader calls.
		SourceSnapshotRef snapshot;
		Container container = Container::Unknown;
		std::optional<OmfRevision> omfRevision; ///< Established HEAD context; raw version values remain properties.
		PropertyLocator embedding;				///< Parent chunk/property, when this is an embedded source.
		QVector<ParsedSource> embeddedSources;	///< Each embedded graph has its own source receipt and local handles.
		QVector<RecordSet> recordSets;
		QVector<AvidObject> objects;
		QVector<Relationship> relationships;
		QVector<RawProperty> unownedProperties;
		QStringList diagnostics;
	};

	// A scan retains the collected source records in its chosen RAM storage.
	// Matching and file-operation checks use these small receipts; callers
	// explicitly restore a graph when they need its original records.
	struct StoredSource
	{
		ParsedSource::Outcome outcome = ParsedSource::Outcome::NotRead;
		QString readReason;
		SourceSnapshotRef snapshot;
		ParsedSource::Container container = ParsedSource::Container::Unknown;
		QStringList diagnostics;
		QSharedPointer<const SourceArchive> archive;
		QSharedPointer<const SourceStore> storage; ///< Alternative backing; ordinary MediaEngine uses archive.
		// If packing is cancelled, keep the obtained graph without losing facts.
		QSharedPointer<const ParsedSource> unfinishedGraph;

		static StoredSource store(ParsedSource &&source, const Cancellation &cancellation);
		std::optional<ParsedSource> restore(const Cancellation &cancellation) const;
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
		QVector<StoredSource> sources;
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
		explicit Cancellation(const std::atomic_bool *external = nullptr) : m_external(external) {}
		void cancel() { m_cancelled.store(true); }
		bool cancelled() const { return m_cancelled.load() || (m_external && m_external->load()); }

	private:
		const std::atomic_bool *m_external = nullptr; // Borrowed for this operation's lifetime.
		std::atomic_bool m_cancelled{false};
	};
}
