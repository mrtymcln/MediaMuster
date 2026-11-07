#pragma once

// Source-local object graphs and reading control for the AVB reader.

#include "mediaevidence.h"
#include <QByteArray>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <atomic>
#include <optional>

namespace MediaEngine
{
	class Cancellation;
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

	struct ByteRange
	{
		qint64 offset = 0;
		qint64 length = 0;
	};

	struct PropertyLocator
	{
		QString name;
		QByteArray key; ///< Original source tag or key.
		quint64 objectNumber = 0;
		QVector<ByteRange> ranges; ///< Empty when not located; multiple ranges retain fragmented values.
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
		bool bytesRetained = true;						  ///< False: locator ranges reference bytes not copied into RAM.
	};

	struct AvidObject
	{
		enum class Role
		{
			Unknown,
			Master,
			FileSource,
			Composition = 4,
			Descriptor,
			Track,
			Component
		};
		ObjectHandle handle = 0;
		Role role = Role::Unknown;
		SourceSnapshotRef snapshot;
		QByteArray recordedIdentity;
		QString identityEncoding;				 ///< Reader-established encoding, not an assumed byte order.
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
		SourceSnapshotRef snapshot;
		Container container = Container::Unknown;
		QVector<RecordSet> recordSets;
		QVector<AvidObject> objects;
		QVector<Relationship> relationships;
		QVector<RawProperty> unownedProperties;
		QStringList diagnostics;
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
