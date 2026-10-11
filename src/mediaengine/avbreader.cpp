// Reads a bin's document header and indexed chunks without following their
// references. Each chunk keeps its original number even when its private layout
// is unknown. Bin membership and view settings are source evidence; sequence
// selection and media matching belong to the separate reference resolver.
// Framing/layout evidence: pyavb file.py, bin.py and ioctx.py.

#include "avbreader.h"
#include "sourcestorage_p.h"
#include "avbobjects_p.h"

#include <QFileDevice>
#include <QtEndian>
#include <algorithm>
#include <array>

namespace MediaEngine
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		using Detail::AvbFieldReader;
		using Detail::AvbReadFailure;

		void check(const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				throw AvbReadFailure{Outcome::Cancelled, QStringLiteral("AVB read cancelled.")};
		}

		int severity(Outcome outcome)
		{
			switch (outcome)
			{
			case Outcome::Cancelled:
				return 6;
			case Outcome::IoError:
				return 5;
			case Outcome::Malformed:
				return 4;
			case Outcome::Unsupported:
				return 3;
			case Outcome::Incomplete:
				return 2;
			case Outcome::Complete:
				return 1;
			case Outcome::NotRead:
				return 0;
			}
			Q_UNREACHABLE();
		}

		void qualify(ParsedSource &result, Outcome outcome, const QString &reason)
		{
			if (severity(outcome) > severity(result.outcome))
				result.outcome = outcome;
			result.diagnostics.append(reason);
		}

		void rangeOnly(QVector<RawProperty> &properties, const QString &name, ObjectHandle object,
					   ByteRange range, const QString &reason)
		{
			auto &property = properties.emplaceBack();
			property.locator.name = name;
			property.locator.objectNumber = object;
			property.locator.ranges.append(range);
			property.bytesRetained = false;
			property.interpretation = reason;
		}

		void retained(QVector<RawProperty> &properties, const QString &name, ObjectHandle object,
					  qint64 offset, QByteArray bytes, const QString &reason)
		{
			auto &property = properties.emplaceBack();
			property.locator.name = name;
			property.locator.objectNumber = object;
			property.locator.ranges.append({offset, bytes.size()});
			property.encoding = std::move(bytes);
			property.state = PropertyReadState::Present;
			property.interpretation = reason;
		}

		class Input
		{
		public:
			Input(QIODevice &device, const Cancellation &cancellation)
				: m_device(device), m_cancellation(cancellation), m_extent(device.size()),
				  m_file(qobject_cast<QFileDevice *>(&device)),
				  m_modified(m_file ? m_file->fileTime(QFileDevice::FileModificationTime) : QDateTime{})
			{
				if (m_extent < 0 || !m_device.seek(0))
					throw AvbReadFailure{Outcome::IoError, QStringLiteral("Cannot determine AVB length or seek to its beginning.")};
			}

			qint64 position() const { return m_position; }
			qint64 remaining() const { return m_extent - m_position; }
			QDateTime modified() const { return m_modified; }

			void append(QByteArray &bytes, qint64 count)
			{
				if (count < 0)
					throw AvbReadFailure{Outcome::Malformed, QStringLiteral("Negative AVB read length.")};
				std::array<char, 16384> buffer{};
				while (count)
				{
					check(m_cancellation);
					if (!remaining())
						throw AvbReadFailure{Outcome::Incomplete, QStringLiteral("AVB ends during a field at byte %1.").arg(m_position)};
					const qint64 request = std::min({count, remaining(), qint64(buffer.size())});
					const qint64 received = m_device.read(buffer.data(), request);
					if (received <= 0)
						throw AvbReadFailure{received == 0 && m_device.atEnd() ? Outcome::Incomplete : Outcome::IoError,
										 QStringLiteral("Cannot read AVB at byte %1: %2").arg(m_position).arg(m_device.errorString())};
					bytes.append(buffer.data(), received);
					m_position += received;
					count -= received;
				}
				check(m_cancellation);
			}

			void skip(qint64 count)
			{
				check(m_cancellation);
				if (count < 0 || count > remaining())
					throw AvbReadFailure{Outcome::Incomplete, QStringLiteral("AVB chunk extends beyond the available file.")};
				if (!m_device.seek(m_position + count))
					throw AvbReadFailure{Outcome::IoError, QStringLiteral("Cannot seek past AVB chunk: %1").arg(m_device.errorString())};
				m_position += count;
			}

			void finish() const
			{
				check(m_cancellation);
				if (m_device.size() != m_extent || (m_file && m_file->fileTime(QFileDevice::FileModificationTime) != m_modified))
					throw AvbReadFailure{Outcome::Incomplete, QStringLiteral("AVB source size or modification time changed during reading; check this source again.")};
			}

		private:
			QIODevice &m_device;
			const Cancellation &m_cancellation;
			const qint64 m_extent;
			QFileDevice *const m_file;
			const QDateTime m_modified;
			qint64 m_position = 0;
		};

		QString indexed(const QString &name, qint64 index)
		{
			return name + QLatin1Char('[') + QString::number(index) + QStringLiteral("]");
		}

		void version(AvbFieldReader &cursor, quint8 value)
		{
			cursor.expectTag(2);
			cursor.expectTag(value);
		}

		void colour(AvbFieldReader &cursor, const QString &name)
		{
			if (cursor.s16(name + QStringLiteral(".version")) != 1)
				cursor.failUnsupported(QStringLiteral("Unrecognised AVB colour layout in %1.").arg(name));
			cursor.u16(name + QStringLiteral(".red"));
			cursor.u16(name + QStringLiteral(".green"));
			cursor.u16(name + QStringLiteral(".blue"));
		}

		void bin(AvbFieldReader &cursor, bool first)
		{
			cursor.expectTag(2);
			const auto layout = cursor.u8(QStringLiteral("Bin.version"));
			if (layout != 14 && layout != 15)
				cursor.failUnsupported(QStringLiteral("AVB Bin version %1 has no established layout.").arg(layout));
			cursor.readObjectReference(QStringLiteral("Bin.view_setting"));
			cursor.u64(QStringLiteral("Bin.uid"));
			const quint32 count = layout == 14 ? cursor.u16(QStringLiteral("Bin.item_count"))
											   : cursor.u32(QStringLiteral("Bin.item_count"));
			cursor.requireCount(count, 13);
			for (quint32 i = 0; i < count; ++i)
			{
				const auto item = indexed(QStringLiteral("Bin.items"), i);
				cursor.readObjectReference(item + QStringLiteral(".mob"));
				cursor.s16(item + QStringLiteral(".x"));
				cursor.s16(item + QStringLiteral(".y"));
				cursor.s32(item + QStringLiteral(".keyframe"));
				cursor.boolean(item + QStringLiteral(".user_placed"));
			}
			cursor.s32(QStringLiteral("Bin.display_mask"));
			cursor.s16(QStringLiteral("Bin.display_mode"));
			cursor.boolean(QStringLiteral("Bin.sifted"));
			for (int i = 0; i < 6; ++i)
			{
				const auto sift = indexed(QStringLiteral("Bin.sifted_settings"), i);
				cursor.s16(sift + QStringLiteral(".method"));
				cursor.string(sift + QStringLiteral(".string"));
				cursor.string(sift + QStringLiteral(".column"));
			}
			const auto sorts = cursor.s16(QStringLiteral("Bin.sort_column_count"));
			cursor.requireCount(sorts, 3);
			for (qint16 i = 0; i < sorts; ++i)
			{
				const auto column = indexed(QStringLiteral("Bin.sort_columns"), i);
				cursor.u8(column + QStringLiteral(".direction"));
				cursor.string(column + QStringLiteral(".column"));
			}
			cursor.s16(QStringLiteral("Bin.mac_font"));
			cursor.s16(QStringLiteral("Bin.mac_font_size"));
			cursor.s16(QStringLiteral("Bin.mac_image_scale"));
			if (cursor.s16(QStringLiteral("Bin.home_rect.version")) != 1)
				cursor.failUnsupported(QStringLiteral("Unrecognised AVB Bin rectangle layout."));
			for (int i = 0; i < 4; ++i)
				cursor.s16(indexed(QStringLiteral("Bin.home_rect.coordinates"), i));
			colour(cursor, QStringLiteral("Bin.background_color"));
			colour(cursor, QStringLiteral("Bin.forground_color"));
			cursor.s16(QStringLiteral("Bin.ql_image_scale"));
			cursor.readObjectReference(QStringLiteral("Bin.attributes"));
			cursor.boolean(QStringLiteral("Bin.was_iconic"));
			if (first)
			{
				version(cursor, 1);
				cursor.s32(QStringLiteral("BinFirst.unknown_s32"));
			}
			cursor.expectTag(3);
		}

		void viewSetting(AvbFieldReader &cursor)
		{
			version(cursor, 6);
			cursor.string(QStringLiteral("Setting.name"));
			cursor.string(QStringLiteral("Setting.kind"));
			cursor.s16(QStringLiteral("Setting.attr_count"));
			cursor.s16(QStringLiteral("Setting.attr_type"));
			cursor.readObjectReference(QStringLiteral("Setting.attributes"));
			version(cursor, 10);
			const auto count = cursor.u16(QStringLiteral("BinViewSetting.column_count"));
			cursor.requireCount(count, 7);
			for (quint16 i = 0; i < count; ++i)
			{
				const auto column = indexed(QStringLiteral("BinViewSetting.columns"), i);
				cursor.string(column + QStringLiteral(".title"));
				cursor.s16(column + QStringLiteral(".format"));
				cursor.s16(column + QStringLiteral(".type"));
				cursor.boolean(column + QStringLiteral(".hidden"));
			}
			for (int tag = cursor.readExtensionTag(); tag >= 0; tag = cursor.readExtensionTag())
			{
				if (tag != 1)
					cursor.failUnsupported(QStringLiteral("BinViewSetting extension %1 has no established layout.").arg(tag));
				cursor.expectTag(69);
				const auto descriptors = cursor.s16(QStringLiteral("BinViewSetting.format_descriptor_count"));
				cursor.requireCount(descriptors, 13);
				for (qint16 i = 0; i < descriptors; ++i)
				{
					const auto descriptor = indexed(QStringLiteral("BinViewSetting.format_descriptors"), i);
					cursor.expectTag(69);
					cursor.s16(descriptor + QStringLiteral(".vcid_free_column_id"));
					cursor.expectTag(71);
					const auto size = cursor.s32(descriptor + QStringLiteral(".data_size"));
					cursor.expectTag(76);
					// The reference reader preserves a four-byte prefix but the writer
					// gives it two words. Retain exact bytes without guessing its meaning.
					cursor.readBytes(descriptor + QStringLiteral(".text_prefix"), 4);
					cursor.readText(descriptor + QStringLiteral(".format_descriptor"), size, TextEncoding::Utf8);
				}
			}
			cursor.expectTag(3);
		}

		class Reader
		{
		public:
			Reader(Input &input, ParsedSource &result, const Cancellation &cancellation)
				: m_input(input), m_result(result), m_cancellation(cancellation) {}

			void read()
			{
				header();
				auto &table = m_result.recordSets.emplaceBack();
				table.name = QStringLiteral("AVB.ObjectTable");
				table.version = 4;
				table.declaredCount = m_count;
				if (m_count > quint64(m_input.remaining() / 8))
					qualify(m_result, Outcome::Incomplete, QStringLiteral("Declared AVB object count cannot fit the available chunk framing."));
				for (quint64 handle = 1; handle <= m_count; ++handle)
				{
					check(m_cancellation);
					if (!m_input.remaining())
						throw AvbReadFailure{Outcome::Incomplete, QStringLiteral("AVB ends before declared object %1.").arg(handle)};
					chunk(handle);
					// Release this object's growth space before reading the next one.
					if (!m_cancellation.cancelled())
						m_result.objects.last().properties.squeeze();
				}
				table.framingComplete = true;
				if (m_input.remaining())
				{
					rangeOnly(m_result.unownedProperties, QStringLiteral("Document.TrailingBytes"), 0,
							  {m_input.position(), m_input.remaining()}, QStringLiteral("Bytes follow the declared object table; no layout assumed."));
					qualify(m_result, Outcome::Incomplete, QStringLiteral("Uninterpreted bytes follow the declared AVB object table."));
				}
			}

			void finishReferences()
			{
				validateReferences();
			}

		private:
			void appendString(QByteArray &bytes)
			{
				const auto offset = bytes.size();
				m_input.append(bytes, 2);
				const auto size = m_bigEndian ? qFromBigEndian<quint16>(bytes.constData() + offset)
											  : qFromLittleEndian<quint16>(bytes.constData() + offset);
				if (size != 0xffff)
					m_input.append(bytes, size);
			}

			void header()
			{
				// Only this small, counted header is assembled; document objects are
				// read independently and unknown payloads are never loaded.
				QByteArray bytes;
				try
				{
					m_input.append(bytes, 2);
					if (bytes == QByteArray::fromHex("0600"))
						m_bigEndian = false;
					else if (bytes == QByteArray::fromHex("0006"))
						m_bigEndian = true;
					else
						throw AvbReadFailure{Outcome::Malformed, QStringLiteral("Invalid AVB byte-order signature.")};
					m_input.append(bytes, 6);
					if (bytes.mid(2) != "Domain")
						throw AvbReadFailure{Outcome::Malformed, QStringLiteral("Invalid AVB Domain signature.")};
					m_result.container = ParsedSource::Container::Avb;
					m_input.append(bytes, 4);
					if (bytes.right(4) != (m_bigEndian ? "OBJD" : "DJBO"))
						throw AvbReadFailure{Outcome::Malformed, QStringLiteral("AVB header is not an OBJD document.")};
					const auto typeOffset = bytes.size();
					appendString(bytes);
					QByteArrayView documentType(bytes.constData() + typeOffset + 2, bytes.size() - typeOffset - 2);
					while (!documentType.empty() && documentType.front() == '\0')
						documentType = documentType.sliced(1);
					while (!documentType.empty() && documentType.back() == '\0')
						documentType = documentType.first(documentType.size() - 1);
					if (documentType != "AObjDoc")
						throw AvbReadFailure{Outcome::Unsupported, QStringLiteral("Unrecognised AVB document type.")};
					m_input.append(bytes, 1);
					if (quint8(bytes.back()) != 4)
						throw AvbReadFailure{Outcome::Unsupported, QStringLiteral("Unrecognised AVB document version.")};
					appendString(bytes);
					m_input.append(bytes, 28);
					appendString(bytes);
					m_input.append(bytes, 16);
				}
				catch (const AvbReadFailure &)
				{
					retained(m_result.unownedProperties, QStringLiteral("Document.UnparsedHeader"), 0, 0,
							 std::move(bytes), QStringLiteral("Partial header retained before input failure."));
					throw;
				}
				AvbFieldReader cursor(bytes, 0, m_bigEndian, m_result, nullptr, m_cancellation);
				try
				{
					cursor.readBytes(QStringLiteral("Document.byte_order"), 2);
					cursor.readText(QStringLiteral("Document.magic"), 6, TextEncoding::Ascii);
					if (cursor.readFourcc(QStringLiteral("Document.class_id")) != "OBJD")
						cursor.failMalformed(QStringLiteral("AVB header is not an OBJD document."));
					if (cursor.string(QStringLiteral("Document.type")) != QStringLiteral("AObjDoc"))
						cursor.failUnsupported(QStringLiteral("Unrecognised AVB document type."));
					if (cursor.u8(QStringLiteral("Document.version")) != 4)
						cursor.failUnsupported(QStringLiteral("Unrecognised AVB document version."));
					cursor.string(QStringLiteral("Document.last_save_text"));
					m_count = cursor.u32(QStringLiteral("Document.object_count"));
					m_root = cursor.readObjectReference(QStringLiteral("Header.root_index"));
					m_rootRead = true;
					const auto marker = cursor.u32(QStringLiteral("Document.byte_order_marker"));
					if (marker != 0x49494949 && marker != 0x4d4d4d4d)
						cursor.failUnsupported(QStringLiteral("Unrecognised AVB document byte-order marker."));
					if (marker != (m_bigEndian ? 0x4d4d4d4d : 0x49494949))
						qualify(m_result, Outcome::Malformed, QStringLiteral("AVB document byte-order marker contradicts its opening signature."));
					cursor.u32(QStringLiteral("Document.last_save"));
					cursor.readBytes(QStringLiteral("Document.reserved_word"), 4);
					if (cursor.readFourcc(QStringLiteral("Document.object_marker")) != "ATob" ||
						cursor.readFourcc(QStringLiteral("Document.version_marker")) != "ATve")
						cursor.failMalformed(QStringLiteral("Invalid AVB document metadata markers."));
					cursor.string(QStringLiteral("Document.creator_version"));
					cursor.readBytes(QStringLiteral("Document.reserved"), 16);
				}
				catch (const AvbReadFailure &)
				{
					retainRemainder(cursor, bytes, 0, nullptr);
					throw;
				}
			}

			void retainRemainder(const AvbFieldReader &cursor, const QByteArray &bytes, qint64 offset, AvidObject *object)
			{
				if (!cursor.remainingBytes())
					return;
				auto &properties = object ? object->properties : m_result.unownedProperties;
				retained(properties, QStringLiteral("UnparsedTail"), object ? object->handle : 0, cursor.fileOffset(),
						 bytes.mid(cursor.fileOffset() - offset), QStringLiteral("No field boundaries are assumed after the unsupported or incomplete layout."));
			}

			void chunk(ObjectHandle handle)
			{
				const auto offset = m_input.position();
				QByteArray framing;
				try
				{
					m_input.append(framing, 8);
				}
				catch (const AvbReadFailure &)
				{
					retained(m_result.unownedProperties, QStringLiteral("Document.PartialChunkHeader"), 0, offset,
							 std::move(framing), QStringLiteral("Incomplete framing cannot establish another object's payload."));
					throw;
				}
				auto &object = m_result.objects.emplaceBack();
				object.handle = handle;
				m_result.recordSets.last().objects.append(handle);
				auto native = QSharedPointer<AvbObjectContext>::create();
				object.avb = native;
				native->bigEndian = m_bigEndian;
				native->framing = {offset, 8};
				AvbFieldReader header(framing, offset, m_bigEndian, m_result, &object, m_cancellation);
				native->classId = header.readFourcc(QStringLiteral("Chunk.class_id"));
				const auto size = header.u32(QStringLiteral("Chunk.size"));
				native->value = {m_input.position(), std::min(qint64(size), m_input.remaining())};
				if (size > m_input.remaining())
				{
					rangeOnly(object.properties, QStringLiteral("UnparsedPayload"), handle, native->value,
							  QStringLiteral("Declared chunk length exceeds available bytes; available range retained."));
					throw AvbReadFailure{Outcome::Incomplete, QStringLiteral("AVB object %1 declares a payload beyond the available file.").arg(handle)};
				}
				const QByteArrayView classId(native->classId);
				const bool isBin = classId == "ABIN" || classId == "BINF";
				const bool isView = classId == "BVst";
				if (!isBin && !isView && !Detail::supportsAvbComponent(classId) && !Detail::supportsAvbDescriptor(classId))
				{
					rangeOnly(object.properties, QStringLiteral("UnparsedPayload"), handle, native->value,
							  QStringLiteral("Unknown AVB class; source range retained without loading its payload."));
					qualify(m_result, Outcome::Incomplete, QStringLiteral("AVB object %1 has unsupported class %2.").arg(handle).arg(QString::fromLatin1(native->classId)));
					m_input.skip(size);
					return;
				}
				QByteArray payload;
				try
				{
					m_input.append(payload, size);
				}
				catch (const AvbReadFailure &)
				{
					retained(object.properties, QStringLiteral("PartialPayload"), handle, native->value.offset,
							 std::move(payload), QStringLiteral("Read stopped before the object's complete payload was available."));
					throw;
				}
				AvbFieldReader cursor(payload, native->value.offset, m_bigEndian, m_result, &object, m_cancellation);
				try
				{
					if (isBin)
						bin(cursor, classId == "BINF");
					else if (isView)
						viewSetting(cursor);
					else if (!Detail::readAvbComponent(cursor, classId) && !Detail::readAvbDescriptor(cursor, classId))
						cursor.failUnsupported(QStringLiteral("AVB class was admitted without a matching grammar."));
					if (cursor.remainingBytes())
						cursor.failUnsupported(QStringLiteral("Uninterpreted bytes follow the known object layout."));
					native->interpretationComplete = true;
				}
				catch (const AvbReadFailure &failure)
				{
					retainRemainder(cursor, payload, native->value.offset, &object);
					if (failure.outcome == Outcome::Cancelled || failure.outcome == Outcome::IoError)
						throw;
					qualify(m_result, failure.outcome == Outcome::Unsupported ? Outcome::Incomplete : failure.outcome,
							QStringLiteral("AVB object %1 (%2): %3").arg(handle).arg(QString::fromLatin1(native->classId), failure.explanation));
				}
			}

			void validateReferences()
			{
				for (auto &relationship : m_result.relationships)
				{
					check(m_cancellation);
					if (relationship.referenceEncoding != QStringLiteral("AVB.ObjectIndex"))
						continue;
					const auto recordedIndex = relationship.recordedReference.toULongLong();
					relationship.target = 0;
					if (!recordedIndex)
						continue;
					if (recordedIndex > quint64(m_result.objects.size()))
					{
						relationship.explanation = QStringLiteral("Recorded AVB object index is outside the available object inventory; no target assigned.");
						qualify(m_result, recordedIndex > m_count ? Outcome::Malformed : Outcome::Incomplete,
								QStringLiteral("AVB %1 refers to unavailable object %2.")
									.arg(relationship.locator.name)
									.arg(recordedIndex));
					}
					else
					{
						relationship.target = recordedIndex;
						relationship.explanation = QStringLiteral("Recorded AVB object index identifies an encountered chunk; this does not prove that chunk's interpretation is complete.");
					}
				}
				if (!m_rootRead)
					return;
				if (!m_root || m_root > quint64(m_result.objects.size()))
				{
					qualify(m_result, !m_root || m_root > m_count ? Outcome::Malformed : Outcome::Incomplete,
							QStringLiteral("AVB document root is null or outside the available object inventory."));
					return;
				}
				const auto &root = m_result.objects[qsizetype(m_root - 1)];
				if (!root.avb || (root.avb->classId != "ABIN" && root.avb->classId != "BINF"))
					qualify(m_result, Outcome::Malformed, QStringLiteral("AVB document root does not identify a bin object."));
			}

			Input &m_input;
			ParsedSource &m_result;
			const Cancellation &m_cancellation;
			bool m_bigEndian = false;
			bool m_rootRead = false;
			quint32 m_count = 0;
			quint32 m_root = 0;
		};
	}

	ParsedSource AvbReader::read(QIODevice &source, const ReaderContext &context) const
	{
		ParsedSource result;
		QDateTime observedModified;
		try
		{
			check(context.cancellation);
			if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
				throw AvbReadFailure{Outcome::IoError, QStringLiteral("AVB reader requires an open readable seekable binary device.")};
			Input input(source, context.cancellation);
			observedModified = input.modified();
			result.outcome = Outcome::Complete;
			Reader reader(input, result, context.cancellation);
			try
			{
				reader.read();
				input.finish();
			}
			catch (const AvbReadFailure &failure)
			{
				qualify(result, failure.outcome, failure.explanation);
				if (input.remaining())
					rangeOnly(result.unownedProperties, QStringLiteral("Document.UnparsedTail"), 0,
							  {input.position(), input.remaining()}, QStringLiteral("Reading stopped; revalidate this source before rereading this range."));
			}
			reader.finishReferences();
		}
		catch (const AvbReadFailure &failure)
		{
			qualify(result, failure.outcome, failure.explanation);
		}
		auto snapshot = QSharedPointer<SourceSnapshot>::create(context.snapshot ? *context.snapshot : SourceSnapshot{});
		snapshot->source = MetadataSource::Avb;
		if (observedModified.isValid())
			snapshot->modified = observedModified;
		snapshot->readState = result.outcome == Outcome::Complete  ? SourceReadState::Complete
							  : result.outcome == Outcome::IoError ? SourceReadState::Unreadable
																   : SourceReadState::Incomplete;
		result.snapshot = snapshot;
		for (auto &object : result.objects)
			object.snapshot = snapshot;
		Detail::squeezeSourceStorage(result, context.cancellation);
		return result;
	}
}
