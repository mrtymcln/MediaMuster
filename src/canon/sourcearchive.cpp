#include "sourcearchive.h"

#include <QDataStream>
#include <QHash>
#include <QIODevice>
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>

namespace Canon
{
	namespace
	{
		constexpr qsizetype kBlockBytes = 64 * 1024;

		struct ArchiveBlock
		{
			QByteArray compressed;
			qsizetype originalBytes = 0;
		};

		// This private signal unwinds a partly written/read graph. Public methods
		// return cancellation separately from failures, and never publish half an archive.
		struct Cancelled {};

		void checkCancellation(const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				throw Cancelled{};
		}

		void checkStream(const QDataStream &stream)
		{
			if (stream.status() != QDataStream::Ok)
				throw SourceArchiveError("Cannot encode or restore the source RAM archive");
		}

		void addBytes(qint64 &total, qint64 amount)
		{
			if (amount < 0 || total > std::numeric_limits<qint64>::max() - amount)
				throw SourceArchiveError("Source RAM archive size overflow");
			total += amount;
		}

		void checkEncodingWidth(const QByteArray &bytes)
		{
			if (quint64(bytes.size()) >= std::numeric_limits<quint32>::max())
				throw SourceArchiveError("Byte array exceeds the Qt source archive encoding width");
		}

		void checkEncodingWidth(const QString &text)
		{
			if (quint64(text.size()) > (quint64(std::numeric_limits<quint32>::max()) - 1) / 2)
				throw SourceArchiveError("Text exceeds the Qt source archive encoding width");
		}

		class WriteDevice final : public QIODevice
		{
		public:
			WriteDevice(QVector<ArchiveBlock> &blocks, const Cancellation &cancellation)
				: m_blocks(blocks), m_cancellation(cancellation)
			{
				m_buffer.reserve(kBlockBytes);
				open(WriteOnly | Unbuffered);
			}
			void finish()
			{
				flushBlock();
				m_blocks.squeeze();
			}
			qint64 serializedBytes = 0;
			qint64 compressedBytes = 0;
		protected:
			qint64 readData(char *, qint64) override { return -1; }
			qint64 writeData(const char *data, qint64 size) override
			{
				checkCancellation(m_cancellation);
				const qint64 requested = size;
				addBytes(serializedBytes, size);
				while (size > 0)
				{
					const qint64 count = qMin<qint64>(size, kBlockBytes - m_buffer.size());
					m_buffer.append(data, count);
					data += count;
					size -= count;
					if (m_buffer.size() == kBlockBytes)
						flushBlock();
				}
				return requested;
			}
		private:
			void flushBlock()
			{
				checkCancellation(m_cancellation);
				if (m_buffer.isEmpty())
					return;
				QByteArray compressed = qCompress(m_buffer, 1);
				checkCancellation(m_cancellation);
				if (compressed.isEmpty())
					throw SourceArchiveError("Cannot compress the source RAM archive");
				compressed.squeeze();
				addBytes(compressedBytes, compressed.size());
				m_blocks.append({std::move(compressed), m_buffer.size()});
				m_buffer.resize(0); // Reuse the bounded scratch allocation for the next block.
			}
			QVector<ArchiveBlock> &m_blocks;
			const Cancellation &m_cancellation;
			QByteArray m_buffer;
		};

		class ReadDevice final : public QIODevice
		{
		public:
			ReadDevice(const QVector<ArchiveBlock> &blocks, qint64 bytes,
					   const Cancellation &cancellation)
				: m_blocks(blocks), m_bytes(bytes), m_cancellation(cancellation)
			{
				open(ReadOnly | Unbuffered);
			}
			bool isSequential() const override { return true; }
			bool atEnd() const override { return consumedBytes == m_bytes; }
			qint64 bytesAvailable() const override { return m_bytes - consumedBytes; }
			qint64 consumedBytes = 0;
		protected:
			qint64 writeData(const char *, qint64) override { return -1; }
			qint64 readData(char *destination, qint64 requested) override
			{
				checkCancellation(m_cancellation);
				qint64 copied = 0;
				while (copied < requested && consumedBytes < m_bytes)
				{
					if (m_offset == m_buffer.size())
					{
						if (m_nextBlock == m_blocks.size())
							throw SourceArchiveError("Source RAM archive has missing blocks");
						const auto &block = m_blocks.at(m_nextBlock++);
						if (block.originalBytes <= 0 || block.originalBytes > kBlockBytes)
							throw SourceArchiveError("Invalid source RAM archive block size");
						m_buffer = qUncompress(block.compressed);
						checkCancellation(m_cancellation);
						if (m_buffer.size() != block.originalBytes)
							throw SourceArchiveError("Cannot decompress the source RAM archive");
						m_offset = 0;
					}
					const qint64 count = qMin<qint64>(requested - copied, m_buffer.size() - m_offset);
					if (count > m_bytes - consumedBytes)
						throw SourceArchiveError("Source RAM archive exceeds its recorded size");
					std::memcpy(destination + copied, m_buffer.constData() + m_offset, size_t(count));
					m_offset += count;
					copied += count;
					consumedBytes += count;
				}
				return copied;
			}
		private:
			const QVector<ArchiveBlock> &m_blocks;
			const qint64 m_bytes;
			const Cancellation &m_cancellation;
			qsizetype m_nextBlock = 0;
			qsizetype m_offset = 0;
			QByteArray m_buffer;
		};

		template<class IO, class T> void fields(IO &io, T &v)
		{
			using Value = std::remove_const_t<T>;
			if constexpr (std::is_same_v<Value, ByteRange>)
				io(v.offset, v.length);
			else if constexpr (std::is_same_v<Value, PropertyLocator>)
				io(v.name, v.key, v.objectNumber, v.ranges);
			else if constexpr (std::is_same_v<Value, BentoPropertyContext>)
				io(v.property, v.type, v.generation, v.referenceListObject, v.typeName,
				   v.tocRanges, v.metadataBigEndian);
			else if constexpr (std::is_same_v<Value, MxfPropertyContext>)
				io(v.localTag, v.primerOffset, v.mappedAuid, v.typeName, v.framingBytes, v.framingRanges);
			else if constexpr (std::is_same_v<Value, MxfSetContext>)
				io(v.key, v.name, v.partitionOffset, v.framing, v.value);
			else if constexpr (std::is_same_v<Value, AvbObjectContext>)
				io(v.classId, v.framing, v.value, v.bigEndian, v.interpretationComplete);
			else if constexpr (std::is_same_v<Value, RawProperty>)
				io(v.locator, v.encoding, v.decoded, v.state, v.interpretation, v.textEncoding,
				   v.textEncodingBasis, v.bento, v.mxf, v.bytesRetained);
			else if constexpr (std::is_same_v<Value, AvidObject>)
				io(v.handle, v.role, v.snapshot, v.recordedIdentity, v.identityEncoding, v.mxf, v.avb, v.properties);
			else if constexpr (std::is_same_v<Value, Relationship>)
				io(v.origin, v.target, v.locator, v.recordedReference, v.referenceEncoding, v.basis, v.explanation);
			else if constexpr (std::is_same_v<Value, RecordSet>)
				io(v.name, v.pmrFileSet, v.version, v.declaredCount, v.objects, v.framingComplete);
			else if constexpr (std::is_same_v<Value, ParsedSource>)
				io(v.outcome, v.readReason, v.snapshot, v.container, v.omfRevision, v.embedding,
				   v.embeddedSources, v.recordSets, v.objects, v.relationships, v.unownedProperties, v.diagnostics);
		}

		template<class T> constexpr bool isRecord =
			std::is_same_v<T, ByteRange> || std::is_same_v<T, PropertyLocator> ||
			std::is_same_v<T, BentoPropertyContext> || std::is_same_v<T, MxfPropertyContext> ||
			std::is_same_v<T, MxfSetContext> || std::is_same_v<T, AvbObjectContext> ||
			std::is_same_v<T, RawProperty> || std::is_same_v<T, AvidObject> ||
			std::is_same_v<T, Relationship> || std::is_same_v<T, RecordSet> || std::is_same_v<T, ParsedSource>;

		// A context is written once and subsequent references use its index. This
		// preserves sharing without keeping those expanded contexts in the archive.
		template<class T> using PointerIds = QHash<const T *, quint64>;
		template<class T> using RestoredPointers = QVector<QSharedPointer<const T>>;
		using WritePointers = std::tuple<PointerIds<BentoPropertyContext>, PointerIds<MxfPropertyContext>,
			PointerIds<MxfSetContext>, PointerIds<AvbObjectContext>>;
		using ReadPointers = std::tuple<RestoredPointers<BentoPropertyContext>, RestoredPointers<MxfPropertyContext>,
			RestoredPointers<MxfSetContext>, RestoredPointers<AvbObjectContext>>;
		enum class VariantEncoding : quint8 { QtValue, Float, Double, List, Map, Hash };

		void configure(QDataStream &stream)
		{
			stream.setVersion(QDataStream::Qt_6_0);
			stream.setByteOrder(QDataStream::BigEndian);
			stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
		}

		class Writer
		{
		public:
			Writer(WriteDevice &device, QVector<SourceSnapshotRef> &receipts, const Cancellation &cancellation)
				: m_stream(&device), m_receipts(receipts), m_cancellation(cancellation) { configure(m_stream); }
			template<class... T> void operator()(const T &...v) { (value(v), ...); }
			template<class T> void value(const T &v)
			{
				checkCancellation(m_cancellation);
				if constexpr (std::is_same_v<T, QByteArray> || std::is_same_v<T, QString>)
					checkEncodingWidth(v);
				if constexpr (std::is_enum_v<T>)
					m_stream << qint32(v);
				else if constexpr (isRecord<T>)
					fields(*this, v);
				else
					m_stream << v;
				checkStream(m_stream);
			}
			template<class T> void value(const std::optional<T> &v)
			{
				value(bool(v));
				if (v) value(*v);
			}
			template<class T> void value(const QList<T> &v)
			{
				value(quint64(v.size()));
				for (const auto &item : v) value(item);
			}
			void value(const SourceSnapshotRef &v)
			{
				if (!v) { value(quint64(0)); return; }
				auto found = m_receiptIds.constFind(v.data());
				if (found != m_receiptIds.cend()) { value(*found); return; }
				const quint64 id = quint64(m_receipts.size()) + 1;
				m_receiptIds.insert(v.data(), id);
				m_receipts.append(v);
				value(id);
			}
			template<class T> void value(const QSharedPointer<const T> &v)
			{
				if (!v) { value(quint64(0)); return; }
				auto &ids = std::get<PointerIds<T>>(m_pointerIds);
				auto found = ids.constFind(v.data());
				if (found != ids.cend()) { value(*found); return; }
				const quint64 id = quint64(ids.size()) + 1;
				ids.insert(v.data(), id);
				value(id);
				value(*v);
			}
			void value(const QVariant &v)
			{
				checkCancellation(m_cancellation);
				const int type = v.metaType().id();
				if (type == QMetaType::Float || type == QMetaType::Double)
				{
					// Integer bit copies preserve NaN payloads and signed zero too.
					m_stream << quint8(type == QMetaType::Float ? VariantEncoding::Float : VariantEncoding::Double);
					value(v.isNull());
					if (type == QMetaType::Float) { quint32 bits; std::memcpy(&bits, v.constData(), sizeof(bits)); value(bits); }
					else { quint64 bits; std::memcpy(&bits, v.constData(), sizeof(bits)); value(bits); }
				}
				else if (type == QMetaType::QVariantList)
				{
					m_stream << quint8(VariantEncoding::List);
					value(v.isNull()); value(*static_cast<const QVariantList *>(v.constData()));
				}
				else if (type == QMetaType::QVariantMap || type == QMetaType::QVariantHash)
				{
					m_stream << quint8(type == QMetaType::QVariantMap ? VariantEncoding::Map : VariantEncoding::Hash);
					value(v.isNull());
					if (type == QMetaType::QVariantMap)
					{
						const auto &map = *static_cast<const QVariantMap *>(v.constData());
						value(quint64(map.size()));
						for (auto it = map.cbegin(); it != map.cend(); ++it) { value(it.key()); value(it.value()); }
					}
					else
					{
						const auto &hash = *static_cast<const QVariantHash *>(v.constData());
						auto keys = hash.keys(); std::sort(keys.begin(), keys.end());
						value(quint64(keys.size()));
						for (const auto &key : keys) { value(key); value(hash.value(key)); }
					}
				}
				else
				{
					if (v.isValid() && !v.metaType().hasRegisteredDataStreamOperators())
						throw SourceArchiveError("Decoded value has no lossless Qt stream encoding");
					if (type == QMetaType::QByteArray) checkEncodingWidth(*static_cast<const QByteArray *>(v.constData()));
					else if (type == QMetaType::QString) checkEncodingWidth(*static_cast<const QString *>(v.constData()));
					else if (type == QMetaType::QStringList)
					{
						const auto &strings = *static_cast<const QStringList *>(v.constData());
						if (quint64(strings.size()) >= std::numeric_limits<quint32>::max())
							throw SourceArchiveError("String list exceeds the Qt source archive encoding width");
						for (const auto &text : strings) { checkCancellation(m_cancellation); checkEncodingWidth(text); }
					}
					m_stream << quint8(VariantEncoding::QtValue) << v;
				}
				checkStream(m_stream);
			}
		private:
			QDataStream m_stream;
			QVector<SourceSnapshotRef> &m_receipts;
			PointerIds<SourceSnapshot> m_receiptIds;
			WritePointers m_pointerIds;
			const Cancellation &m_cancellation;
		};

		class Reader
		{
		public:
			Reader(ReadDevice &device, const QVector<SourceSnapshotRef> &receipts, const Cancellation &cancellation)
				: m_stream(&device), m_device(device), m_receipts(receipts), m_cancellation(cancellation) { configure(m_stream); }
			template<class... T> void operator()(T &...v) { (value(v), ...); }
			template<class T> void value(T &v)
			{
				checkCancellation(m_cancellation);
				if constexpr (std::is_enum_v<T>) { qint32 encoded = 0; m_stream >> encoded; v = static_cast<T>(encoded); }
				else if constexpr (isRecord<T>) fields(*this, v);
				else m_stream >> v;
				checkStream(m_stream);
			}
			qsizetype count()
			{
				quint64 encoded = 0; value(encoded);
				// Every stored item has at least one byte. This checks the archive's
				// own recorded length without imposing a memory budget on the graph.
				if (encoded > quint64(std::numeric_limits<qsizetype>::max()) || encoded > quint64(m_device.bytesAvailable()))
					throw SourceArchiveError("Invalid source RAM archive item count");
				return qsizetype(encoded);
			}
			template<class T> void value(std::optional<T> &v)
			{
				bool present = false; value(present);
				if (present) { T item{}; value(item); v = std::move(item); }
				else v.reset();
			}
			template<class T> void value(QList<T> &v)
			{
				const qsizetype size = count();
				v.clear(); v.reserve(size);
				for (qsizetype index = 0; index < size; ++index) { T item{}; value(item); v.append(std::move(item)); }
			}
			void value(SourceSnapshotRef &v)
			{
				quint64 id = 0; value(id);
				if (id > quint64(m_receipts.size())) throw SourceArchiveError("Invalid source RAM archive receipt");
				v = id ? m_receipts.at(qsizetype(id - 1)) : SourceSnapshotRef{};
			}
			template<class T> void value(QSharedPointer<const T> &v)
			{
				quint64 id = 0; value(id);
				if (!id) { v.clear(); return; }
				auto &pointers = std::get<RestoredPointers<T>>(m_pointers);
				if (id <= quint64(pointers.size())) { v = pointers.at(qsizetype(id - 1)); return; }
				if (id != quint64(pointers.size()) + 1) throw SourceArchiveError("Invalid source RAM archive context");
				T item{}; value(item);
				v = QSharedPointer<const T>::create(std::move(item));
				pointers.append(v);
			}
			void value(QVariant &v)
			{
				quint8 kind = 0; value(kind);
				if (kind == quint8(VariantEncoding::QtValue)) { m_stream >> v; checkStream(m_stream); return; }
				bool isNull = false; value(isNull);
				if (kind == quint8(VariantEncoding::Float))
				{
					quint32 bits = 0; value(bits); float number; std::memcpy(&number, &bits, sizeof(number));
					v = isNull ? QVariant(QMetaType::fromType<float>()) : QVariant::fromValue(number);
				}
				else if (kind == quint8(VariantEncoding::Double))
				{
					quint64 bits = 0; value(bits); double number; std::memcpy(&number, &bits, sizeof(number));
					v = isNull ? QVariant(QMetaType::fromType<double>()) : QVariant::fromValue(number);
				}
				else if (kind == quint8(VariantEncoding::List))
				{
					QVariantList list; value(list);
					v = isNull ? QVariant(QMetaType::fromType<QVariantList>()) : QVariant::fromValue(std::move(list));
				}
				else if (kind == quint8(VariantEncoding::Map))
				{
					QVariantMap map; const qsizetype size = count();
					for (qsizetype index = 0; index < size; ++index) { QString key; QVariant item; value(key); value(item); map.insert(key, std::move(item)); }
					v = isNull ? QVariant(QMetaType::fromType<QVariantMap>()) : QVariant::fromValue(std::move(map));
				}
				else if (kind == quint8(VariantEncoding::Hash))
				{
					QVariantHash hash; const qsizetype size = count(); hash.reserve(size);
					for (qsizetype index = 0; index < size; ++index) { QString key; QVariant item; value(key); value(item); hash.insert(key, std::move(item)); }
					v = isNull ? QVariant(QMetaType::fromType<QVariantHash>()) : QVariant::fromValue(std::move(hash));
				}
				else throw SourceArchiveError("Invalid source RAM archive variant");
			}
		private:
			QDataStream m_stream;
			ReadDevice &m_device;
			const QVector<SourceSnapshotRef> &m_receipts;
			ReadPointers m_pointers;
			const Cancellation &m_cancellation;
		};
	}

	struct SourceArchive::Data
	{
		QVector<ArchiveBlock> blocks;
		QVector<SourceSnapshotRef> receipts;
		qint64 compressedBytes = 0;
		qint64 serializedBytes = 0;
	};

	SourceArchive::SourceArchive(QSharedPointer<const Data> data) : m_data(std::move(data)) {}

	QSharedPointer<const SourceArchive> SourceArchive::pack(const ParsedSource &source, const Cancellation &cancellation)
	{
		try
		{
			checkCancellation(cancellation);
			auto data = QSharedPointer<Data>::create();
			WriteDevice device(data->blocks, cancellation);
			Writer writer(device, data->receipts, cancellation);
			writer.value(source);
			device.finish();
			data->receipts.squeeze();
			data->compressedBytes = device.compressedBytes;
			data->serializedBytes = device.serializedBytes;
			checkCancellation(cancellation);
			return QSharedPointer<SourceArchive>::create(std::move(data));
		}
		catch (const Cancelled &) { return {}; }
		catch (const std::bad_alloc &) { throw; }
		catch (const SourceArchiveError &) { throw; }
		catch (const std::exception &error) { throw SourceArchiveError(error.what()); }
	}

	std::optional<ParsedSource> SourceArchive::restore(const Cancellation &cancellation) const
	{
		try
		{
			checkCancellation(cancellation);
			ReadDevice device(m_data->blocks, m_data->serializedBytes, cancellation);
			Reader reader(device, m_data->receipts, cancellation);
			ParsedSource source;
			reader.value(source);
			if (!device.atEnd()) throw SourceArchiveError("Source RAM archive has trailing bytes");
			checkCancellation(cancellation);
			return source;
		}
		catch (const Cancelled &) { return std::nullopt; }
		catch (const std::bad_alloc &) { throw; }
		catch (const SourceArchiveError &) { throw; }
		catch (const std::exception &error) { throw SourceArchiveError(error.what()); }
	}

	qint64 SourceArchive::compressedBytes() const { return m_data->compressedBytes; }
	qint64 SourceArchive::serializedBytes() const { return m_data->serializedBytes; }
	qsizetype SourceArchive::blockCount() const { return m_data->blocks.size(); }

	StoredSource StoredSource::store(ParsedSource &&source, const Cancellation &cancellation)
	{
		StoredSource stored;
		stored.outcome = source.outcome;
		stored.readReason = source.readReason;
		stored.snapshot = source.snapshot;
		stored.container = source.container;
		stored.diagnostics = source.diagnostics;
		stored.archive = SourceArchive::pack(source, cancellation);
		if (stored.archive)
			source = {}; // Consume the rvalue caller's expanded graph after packing.
		else
			stored.unfinishedGraph = QSharedPointer<const ParsedSource>::create(std::move(source));
		return stored;
	}

	std::optional<ParsedSource> StoredSource::restore(const Cancellation &cancellation) const
	{
		if (cancellation.cancelled()) return std::nullopt;
		if (archive) return archive->restore(cancellation);
		if (unfinishedGraph)
		{
			ParsedSource source = *unfinishedGraph;
			if (cancellation.cancelled()) return std::nullopt;
			return source;
		}
		if (outcome != ParsedSource::Outcome::NotRead)
			throw SourceArchiveError("A parsed source has no stored graph");
		// Unopened headers only have these scheduling/receipt fields.
		ParsedSource source;
		source.outcome = outcome;
		source.readReason = readReason;
		source.snapshot = snapshot;
		source.container = container;
		source.diagnostics = diagnostics;
		if (cancellation.cancelled()) return std::nullopt;
		return source;
	}
}
