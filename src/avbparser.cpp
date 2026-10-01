#include "avbparser.h"
#include "avidtext.h"
#include "diagnostics.h"
#include "mobid.h"
#include "omfuid.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>

// Remember where the objects are so we don't have to keep the whole file in memory.
namespace
{
	using namespace std::string_view_literals;

	constexpr auto kLittleEndianAvbHeader =
		"\x06\x00"
		"DomainDJBO\x07\x00"
		"AObjDoc"sv;

	constexpr auto kBigEndianAvbHeader =
		"\x00\x06"
		"DomainOBJD\x00\x07"
		"AObjDoc"sv;

	static_assert(kLittleEndianAvbHeader.size() == kBigEndianAvbHeader.size());

	constexpr int kMaxWarnings = 32;
	constexpr quint16 kNullStringLength = 0xffff;

	constexpr int kMobLabelBytes = 12;
	constexpr int kMobMaterialOffset = 16;
	constexpr int kMobTailOffset = 24;
	constexpr int kMobTailBytes = 8;

	constexpr quint8 kDocumentVersion = 4;
	constexpr quint8 kComponentVersion = 3;
	constexpr quint8 kClipVersion = 1;
	constexpr quint8 kTrackGroupVersion = 8;
	constexpr quint8 kCompositionVersion = 2;
	constexpr quint8 kSourceClipVersion = 3;
	constexpr quint8 kBinVersion = 0x0e;
	constexpr quint8 kLargeBinVersion = 0x0f;

	constexpr quint16 kTrackLabel = 1 << 0;
	constexpr quint16 kTrackAttributes = 1 << 1;
	constexpr quint16 kTrackComponent = 1 << 2;
	constexpr quint16 kTrackFillerProxy = 1 << 3;
	constexpr quint16 kTrackBob = 1 << 4;
	constexpr quint16 kTrackControlCode = 1 << 5;
	constexpr quint16 kTrackControlSubCode = 1 << 6;
	constexpr quint16 kTrackStartPosition = 1 << 7;
	constexpr quint16 kTrackReadOnly = 1 << 8;
	constexpr quint16 kTrackSessionAttributes = 1 << 9;
	constexpr quint16 kUnknownTrackFlags = 0xfc00;

	enum class AvbPropertyTag : quint8
	{
		Extension = 1,
		Class = 2,
		End = 3,
		Bytes = 65,
		Boolean = 66,
		UInt8 = 68,
		Int16 = 69,
		UInt16 = 70,
		Int32 = 71,
		UInt32 = 72,
		Double = 75,
		String = 76,
		Int64 = 77
	};

	using RawMobId = std::array<uchar, MobId::kRawSize>;

	struct AvbParserFailure
	{
		QString message;
	};

	struct AvbUnsupported
	{
		QString message;
	};

	// Keep reads inside their assigned range so a bad length can't spill into neighbouring data.
	class AvbValueParser
	{
	public:
		AvbValueParser(QFile &file, qint64 begin, qint64 end, bool little, const std::atomic_bool *cancelled)
			: m_file(file), m_pos(begin), m_end(end), m_little(little), m_cancelled(cancelled)
		{
		}

		qint64 pos() const noexcept
		{
			return m_pos;
		}

		qint64 remaining() const noexcept
		{
			return m_end - m_pos;
		}

		void checkCancelled() const
		{
			if (m_cancelled && m_cancelled->load(std::memory_order_relaxed))
				fail(QStringLiteral("Bin reading cancelled."));
		}

		[[noreturn]] void fail(const QString &reason) const
		{
			throw AvbParserFailure{QStringLiteral("%1 (byte %2)").arg(reason).arg(m_pos)};
		}

		[[noreturn]] void unsupported(const QString &reason) const
		{
			throw AvbUnsupported{QStringLiteral("%1 (byte %2)").arg(reason).arg(m_pos)};
		}

		void read(char *out, qint64 size)
		{
			checkCancelled();
			if (size < 0 || size > remaining())
				fail(QStringLiteral("Truncated AVB property"));
			if ((m_file.pos() != m_pos && !m_file.seek(m_pos)) || m_file.read(out, size) != size)
				fail(QStringLiteral("Cannot read AVB property"));
			m_pos += size;
		}

		QByteArray bytes(qint64 size)
		{
			if (size < 0 || size > remaining())
				fail(QStringLiteral("Invalid AVB string or byte-array length"));
			QByteArray out(size, Qt::Uninitialized);
			read(out.data(), size);
			return out;
		}

		void skip(qint64 size)
		{
			checkCancelled();
			if (size < 0 || size > remaining())
				fail(QStringLiteral("AVB property exceeds its object"));
			// Seeking can go past the end of a file.
			// Read the last skipped byte to make sure it's there.
			if (size)
			{
				char last{};
				if (!m_file.seek(m_pos + size - 1) || m_file.read(&last, 1) != 1)
					fail(QStringLiteral("Truncated AVB object"));
			}
			m_pos += size;
		}

		quint8 u8()
		{
			char v{};
			read(&v, 1);
			return static_cast<quint8>(v);
		}

		quint16 u16()
		{
			std::array<uchar, sizeof(quint16)> b{};
			read(reinterpret_cast<char *>(b.data()), b.size());
			return m_little ? qFromLittleEndian<quint16>(b.data()) : qFromBigEndian<quint16>(b.data());
		}

		quint32 u32()
		{
			std::array<uchar, sizeof(quint32)> b{};
			read(reinterpret_cast<char *>(b.data()), b.size());
			return m_little ? qFromLittleEndian<quint32>(b.data()) : qFromBigEndian<quint32>(b.data());
		}

		qint16 s16()
		{
			std::array<uchar, sizeof(qint16)> b{};
			read(reinterpret_cast<char *>(b.data()), b.size());
			return m_little ? qFromLittleEndian<qint16>(b.data()) : qFromBigEndian<qint16>(b.data());
		}

		qint32 s32()
		{
			std::array<uchar, sizeof(qint32)> b{};
			read(reinterpret_cast<char *>(b.data()), b.size());
			return m_little ? qFromLittleEndian<qint32>(b.data()) : qFromBigEndian<qint32>(b.data());
		}

		quint8 peek()
		{
			const auto before = m_pos;
			const auto value = u8();
			m_pos = before;
			return value;
		}

		void tag(quint8 expected)
		{
			if (u8() != expected)
				fail(QStringLiteral("Invalid AVB property tag; expected 0x%1")
						 .arg(expected, 2, 16, QLatin1Char('0')));
		}

		void tag(AvbPropertyTag expected)
		{
			tag(static_cast<quint8>(expected));
		}

		void start(quint8 version)
		{
			tag(AvbPropertyTag::Class);
			const auto actual = u8();
			if (actual != version)
				unsupported(QStringLiteral("Unsupported AVB object version %1 (expected %2)")
								.arg(actual)
								.arg(version));
		}

		bool extension(quint8 &value)
		{
			if (peek() != static_cast<quint8>(AvbPropertyTag::Extension))
				return false;
			u8();
			value = u8();
			return true;
		}

		[[noreturn]] void unknownExtension(quint8 value) const
		{
			unsupported(QStringLiteral("Unsupported AVB extension 0x%1")
							.arg(value, 2, 16, QLatin1Char('0')));
		}

		void finish()
		{
			tag(AvbPropertyTag::End);
			if (remaining())
				fail(QStringLiteral("Unexpected data after AVB object end"));
		}

		QByteArray fourcc()
		{
			auto value = bytes(4);
			if (m_little)
				std::reverse(value.begin(), value.end());
			return value;
		}

		QString string(bool utf8 = false)
		{
			const auto size = u16();
			if (size == kNullStringLength)
				return {};
			const auto value = bytes(size);
			qsizetype begin = 0;
			while (begin < value.size() && value[begin] == '\0')
				++begin;
			qsizetype end = value.size();
			while (end > begin && value[end - 1] == '\0')
				--end;
			const QByteArrayView text(value.constData() + begin, end - begin);
			if (utf8)
			{
				QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
				const QString decoded = decoder.decode(text);
				if (decoder.hasError())
					fail(QStringLiteral("Invalid UTF-8 AVB string"));
				return decoded;
			}
			QString decoded;
			decoded.reserve(text.size());
			for (const auto c : text)
			{
				const auto b = static_cast<quint8>(c);
				decoded.append(b < 0x80 ? QChar(static_cast<char16_t>(b))
										: QChar(AvidText::kMacRomanHigh[b - 0x80]));
			}
			return decoded;
		}

		quint32 count(qint64 value, quint32 minimumBytes)
		{
			if (value < 0 || value > remaining() / minimumBytes)
				fail(QStringLiteral("Invalid AVB entry count"));
			return static_cast<quint32>(value);
		}

	private:
		QFile &m_file;
		qint64 m_pos;
		qint64 m_end;
		bool m_little;
		const std::atomic_bool *m_cancelled;
	};

	struct AvbObject
	{
		QByteArray type;
		qint64 offset = 0;
		quint32 size = 0;
	};

	struct AvbBinReference
	{
		QString name;
		QString uid;
	};

	struct AvbComponent
	{
		QString name;
		quint32 attributes = 0;
	};

	struct AvbComposition
	{
		AvbMob mob;
		quint32 attributes = 0;
	};

	RawMobId nativeMobId(quint32 low, quint32 high)
	{
		std::array<uchar, OmfUid::kPmrSize> core{};
		qToLittleEndian(low, core.data());
		qToLittleEndian(high, core.data() + sizeof(low));
		return OmfUid::toMobIdBytes(core.data());
	}

	RawMobId typedMobId(AvbValueParser &r)
	{
		RawMobId out{};
		r.tag(AvbPropertyTag::Bytes);
		if (r.u32() != kMobLabelBytes)
			r.fail(QStringLiteral("Invalid MOB label length"));
		r.read(reinterpret_cast<char *>(out.data()), kMobLabelBytes);
		for (int i = kMobLabelBytes; i < kMobMaterialOffset; ++i)
		{
			r.tag(AvbPropertyTag::UInt8);
			out[i] = r.u8();
		}

		r.tag(AvbPropertyTag::UInt32);
		qToLittleEndian(r.u32(), out.data() + kMobMaterialOffset);
		r.tag(AvbPropertyTag::UInt16);
		qToLittleEndian(r.u16(), out.data() + kMobMaterialOffset + sizeof(quint32));
		r.tag(AvbPropertyTag::UInt16);
		qToLittleEndian(r.u16(), out.data() + kMobMaterialOffset + sizeof(quint32) + sizeof(quint16));
		r.tag(AvbPropertyTag::Bytes);
		if (r.u32() != kMobTailBytes)
			r.fail(QStringLiteral("Invalid MOB material length"));
		r.read(reinterpret_cast<char *>(out.data() + kMobTailOffset), kMobTailBytes);
		return out;
	}

	bool isNullMobId(const RawMobId &mob) noexcept
	{
		if (std::all_of(mob.begin(), mob.end(), [](uchar b)
						{ return b == 0; }))
			return true;

		return std::memcmp(mob.data(), OmfUid::kPrefix, sizeof OmfUid::kPrefix) == 0 &&
			   std::memcmp(mob.data() + kMobTailOffset, OmfUid::kSuffix, sizeof OmfUid::kSuffix) == 0 &&
			   std::all_of(mob.begin() + kMobMaterialOffset, mob.begin() + kMobTailOffset,
						   [](uchar b)
						   { return b == 0; });
	}

	class AvbFileParser
	{
	public:
		AvbFileParser(QFile &file, AvbBin &result, const std::atomic_bool *cancelled)
			: m_file(file), m_result(result), m_cancelled(cancelled)
		{
		}

		void parse()
		{
			index();

			m_result.complete = true;
			for (qsizetype id = 1; id < m_objects.size(); ++id)
			{
				const auto &object = m_objects[id];
				AvbValueParser r(m_file, object.offset, object.offset + object.size, m_little, m_cancelled);
				r.checkCancelled();
				try
				{
					parseObject(r, object.type, static_cast<quint32>(id));
				}
				catch (const AvbUnsupported &problem)
				{
					warn(QStringLiteral("Object %1 (%2): %3")
							 .arg(id)
							 .arg(QString::fromLatin1(object.type), problem.message));
				}
			}

			for (auto it = m_compositions.cbegin(); it != m_compositions.cend(); ++it)
			{
				checkCancelled();
				auto mob = it.value().mob;
				const auto binId = m_originalBinRefs.value(it.value().attributes);
				if (binId)
				{
					const auto bin = m_binReferences.constFind(binId);
					if (bin != m_binReferences.cend())
					{
						mob.originalBin = bin->name;
						mob.originalBinUid = bin->uid;
					}
				}
				m_result.mobs.append(std::move(mob));
			}

			std::sort(
				m_result.mobs.begin(), m_result.mobs.end(),
				[this](const auto &a, const auto &b)
				{
					checkCancelled();
					if (a.mobId != b.mobId)
						return a.mobId < b.mobId;
					if (a.name != b.name)
						return a.name < b.name;
					if (a.originalBinUid != b.originalBinUid)
						return a.originalBinUid < b.originalBinUid;
					return a.originalBin < b.originalBin;
				});

			checkCancelled();
			if (m_file.size() != m_size || m_file.fileTime(QFileDevice::FileModificationTime) != m_modified)
				throw AvbParserFailure{QStringLiteral("AVB file changed while reading; load it again.")};
			m_result.valid = true;
		}

	private:
		void checkCancelled() const
		{
			if (m_cancelled && m_cancelled->load(std::memory_order_relaxed))
				throw AvbParserFailure{QStringLiteral("Bin reading cancelled.")};
		}

		void warn(const QString &message)
		{
			m_result.complete = false;
			if (m_result.warnings.size() < kMaxWarnings && !m_result.warnings.contains(message))
				m_result.warnings.append(message);
		}

		void index()
		{
			m_size = m_file.size();
			m_modified = m_file.fileTime(QFileDevice::FileModificationTime);
			if (m_size < 2)
				throw AvbParserFailure{QStringLiteral("AVB file is empty or truncated.")};
			const auto order = m_file.read(2);
			if (order == QByteArray::fromHex("0600"))
				m_little = true;
			else if (order == QByteArray::fromHex("0006"))
				m_little = false;
			else
				throw AvbParserFailure{QStringLiteral("Not an Avid bin: invalid byte-order marker.")};
			AvbValueParser r(m_file, 2, m_size, m_little, m_cancelled);
			if (r.bytes(6) != "Domain" || r.fourcc() != "OBJD" || r.string() != QStringLiteral("AObjDoc"))
				r.fail(QStringLiteral("Not an Avid bin: invalid document header"));
			r.tag(kDocumentVersion);
			r.string();
			const auto count = r.u32();
			m_root = r.u32();
			if (!count || count > static_cast<quint64>(r.remaining()) / 9 || !m_root || m_root > count)
				r.fail(QStringLiteral("Invalid AVB object count or root reference"));
			if (r.u32() != (m_little ? 0x49494949U : 0x4d4d4d4dU))
				r.fail(QStringLiteral("AVB header byte order is inconsistent"));
			r.skip(8);
			if (r.fourcc() != "ATob" || r.fourcc() != "ATve")
				r.fail(QStringLiteral("Invalid AVB document format identifiers"));
			r.string();
			r.skip(16);
			m_objects.reserve(qsizetype(count) + 1);
			// Leave index zero empty because AVB uses it to mean 'no object'.
			m_objects.append(AvbObject{});
			for (qsizetype id = 1; id <= count; ++id)
			{
				const auto type = r.fourcc();
				const auto size = r.u32();
				if (!size || size > r.remaining())
					r.fail(QStringLiteral("Invalid AVB chunk length"));
				if (std::any_of(type.begin(), type.end(), [](char c)
								{ return static_cast<quint8>(c) < 32 || static_cast<quint8>(c) > 126; }))
					r.fail(QStringLiteral("Invalid AVB class identifier"));
				m_objects.append({type, r.pos(), size});
				r.skip(size - 1);
				r.tag(AvbPropertyTag::End);
			}
			if (r.remaining())
				r.fail(QStringLiteral("Unexpected data after declared AVB objects"));
			if (m_objects[m_root].type != "ABIN" && m_objects[m_root].type != "BINF")
				r.fail(QStringLiteral("AVB document root is not a bin"));
		}

		quint32 ref(AvbValueParser &r, const char *expected = nullptr, bool required = false)
		{
			const auto value = r.u32();
			if (value >= m_objects.size() || (required && !value))
				r.fail(QStringLiteral("Invalid AVB object reference %1").arg(value));
			if (value && expected && m_objects[value].type != expected)
				r.fail(QStringLiteral("AVB reference %1 must identify %2")
						   .arg(value)
						   .arg(QString::fromLatin1(expected)));
			return value;
		}

		AvbComponent component(AvbValueParser &r)
		{
			r.start(kComponentVersion);
			ref(r);
			ref(r);
			r.skip(8);
			AvbComponent result;
			result.name = r.string();
			r.string();
			result.attributes = ref(r, "ATTR");
			ref(r);
			ref(r);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				r.tag(AvbPropertyTag::UInt32);
				ref(r);
			}
			return result;
		}

		quint32 trackGroup(AvbValueParser &r)
		{
			r.start(kTrackGroupVersion);
			r.skip(9);
			const auto tracks = r.count(r.u32(), 2);
			for (quint32 i = 0; i < tracks; ++i)
			{
				const auto flags = r.u16();
				if (flags & kUnknownTrackFlags)
					r.unsupported(QStringLiteral("Unsupported AVB track flags"));
				if (flags & kTrackLabel)
					r.skip(2);
				if (flags & kTrackAttributes)
					ref(r);
				if (flags & kTrackSessionAttributes)
					ref(r);
				if (flags & kTrackComponent)
					ref(r);
				if (flags & kTrackFillerProxy)
					ref(r);
				if (flags & kTrackBob)
					ref(r);
				if (flags & kTrackControlCode)
					r.skip(2);
				if (flags & kTrackControlSubCode)
					r.skip(2);
				if (flags & kTrackStartPosition)
					r.skip(4);
				if (flags & kTrackReadOnly)
					r.skip(1);
			}
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				for (quint32 i = 0; i < tracks; ++i)
				{
					r.tag(AvbPropertyTag::Int16);
					r.skip(2);
				}
			}
			return tracks;
		}

		void composition(AvbValueParser &r, quint32 id)
		{
			AvbComposition value;
			const auto base = component(r);
			value.attributes = base.attributes;
			value.mob.name = base.name;
			trackGroup(r);
			r.start(kCompositionVersion);
			const auto low = r.u32();
			const auto high = r.u32();
			auto mob = nativeMobId(low, high);
			r.skip(4);
			value.mob.mobType = r.u8();
			value.mob.usageCode = r.s32();
			ref(r);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag == 1)
				{
					r.tag(AvbPropertyTag::Int32);
					r.skip(4);
				}
				else if (tag == 2)
					mob = typedMobId(r);
				else
					r.unknownExtension(tag);
			}
			r.finish();
			if (!isNullMobId(mob))
			{
				value.mob.mobId = MobId::format(mob.data());
				m_compositions.insert(id, std::move(value));
			}
		}

		void sourceClip(AvbValueParser &r)
		{
			component(r);
			r.start(kClipVersion);
			r.skip(4);
			r.start(kSourceClipVersion);
			r.u32(); // legacy source-reference words
			r.u32();
			r.skip(6);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				typedMobId(r); // Validate the reference without retaining a filter identity.
			}
			r.finish();
		}

		void bin(AvbValueParser &r, const QByteArray &type)
		{
			r.tag(AvbPropertyTag::Class);
			const auto version = r.u8();
			if (version != kBinVersion && version != kLargeBinVersion)
				r.unsupported(QStringLiteral("Unsupported AVB bin version"));
			ref(r);
			r.skip(8);
			const auto items = r.count(version == kBinVersion ? r.u16() : r.u32(), 13);
			for (quint32 i = 0; i < items; ++i)
			{
				ref(r, "CMPO", true);
				r.skip(9);
			}
			r.skip(7);
			for (int i = 0; i < 6; ++i)
			{
				r.skip(2);
				r.string();
				r.string();
			}
			const auto columns = r.count(r.s16(), 3);
			for (quint32 i = 0; i < columns; ++i)
			{
				r.skip(1);
				r.string();
			}
			r.skip(6);
			if (r.u16() != 1)
				r.fail(QStringLiteral("Invalid AVB bin rectangle version"));
			r.skip(8);
			for (int i = 0; i < 2; ++i)
			{
				if (r.u16() != 1)
					r.fail(QStringLiteral("Invalid AVB bin color version"));
				r.skip(6);
			}
			r.skip(2);
			ref(r, "ATTR");
			r.skip(1);
			if (type == "BINF")
			{
				r.start(1);
				r.skip(4);
			}
			r.finish();
		}

		void attributes(AvbValueParser &r, quint32 id)
		{
			r.start(1);
			const auto count = r.count(r.u32(), 6);
			quint32 originalBin = 0;
			for (quint32 i = 0; i < count; ++i)
			{
				const auto type = r.u32();
				const auto name = r.string();
				switch (type)
				{
				case 1:
					r.skip(4);
					break;
				case 2:
					r.string();
					break;
				case 3:
				{
					const bool isOriginal = name == QStringLiteral("_ORG_BIN");
					const auto value = ref(r, isOriginal ? "MCBR" : nullptr);
					if (isOriginal)
						originalBin = value;
					break;
				}
				case 4:
				{
					const auto size = r.u32();
					r.skip(size);
					break;
				}
				default:
					r.unsupported(QStringLiteral("Unsupported AVB attribute type %1").arg(type));
				}
			}
			r.finish();
			if (originalBin)
				m_originalBinRefs.insert(id, originalBin);
		}

		void binReference(AvbValueParser &r, quint32 id)
		{
			r.start(1);
			const auto high = r.u32();
			const auto low = r.u32();
			AvbBinReference value;
			value.uid = QStringLiteral("%1%2")
							.arg(high, 8, 16, QLatin1Char('0'))
							.arg(low, 8, 16, QLatin1Char('0'));
			value.name = r.string();
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				r.tag(AvbPropertyTag::String);
				const auto utf8 = r.string(true);
				if (!utf8.isEmpty())
					value.name = utf8;
			}
			r.finish();
			m_binReferences.insert(id, std::move(value));
		}

		void readMediaLocator(AvbValueParser &r)
		{
			r.start(2);
			const auto low = r.u32();
			const auto high = r.u32();
			r.string();
			RawMobId mob{};
			bool hasFullId = false;
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag == 1)
				{
					r.tag(AvbPropertyTag::Int32);
					r.skip(4);
				}
				else if (tag == 2)
				{
					mob = typedMobId(r);
					hasFullId = true;
				}
				else if (tag == 3)
				{
					r.tag(AvbPropertyTag::String);
					r.string(true);
				}
				else
					r.unknownExtension(tag);
			}
			r.finish();
			// A present full ID is authoritative, including a null ID. Only
			// a missing extension permits using the older scalar fields.
			m_result.mediaFileIds.add(hasFullId
										  ? BinFileId::fromMobId(MobId::format(mob.data()))
										  : BinFileId::fromLegacyWords(low, high));
		}

		void mobReference(AvbValueParser &r, const QByteArray &type)
		{
			r.start(1);
			r.u32(); // legacy reference words
			r.u32();
			if (type == "MCMR" || type == "TMBC")
				r.skip(4);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				typedMobId(r);
			}
			if (type == "TMBC")
			{
				r.start(3);
				r.skip(4);
				ref(r, "ATTR");
				if (r.u16() != 1)
					r.fail(QStringLiteral("Invalid AVB marker color version"));
				r.skip(6);
				while (r.extension(tag))
				{
					if (tag != 1)
						r.unknownExtension(tag);
					r.tag(AvbPropertyTag::Boolean);
					r.skip(1);
				}
			}
			if (type == "ABOB" || type == "DIDP" || type == "MPGP")
			{
				r.start(1);
				r.skip(12);
			}
			if (type == "DIDP" || type == "MPGP")
			{
				r.start(1);
				r.skip(21);
			}
			if (type == "MPGP")
			{
				r.start(1);
				r.skip(3);
				const auto count = r.count(r.s16(), 5);
				if (count)
				{
					r.skip(2);
					r.skip(static_cast<qint64>(count) * 5);
				}
			}
			r.finish();
		}

		void audioPlugin(AvbValueParser &r)
		{
			component(r);
			trackGroup(r);
			r.start(6);
			r.skip(24);
			ref(r);
			r.skip(2);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 2)
					r.unknownExtension(tag);
				r.tag(AvbPropertyTag::UInt32);
				ref(r);
			}
			r.start(1);
			if (r.s32() != 1)
				r.unsupported(QStringLiteral("Unsupported AudioSuite plug-in count"));
			r.string();
			r.skip(12);
			const auto chunks = r.count(r.u32(), 26);
			for (quint32 i = 0; i < chunks; ++i)
			{
				const auto size = r.u32();
				r.skip(20);
				r.string();
				r.skip(size);
			}
			while (r.extension(tag))
			{
				switch (tag)
				{
				case 1:
				{
					r.tag(AvbPropertyTag::Int32);
					r.u32();
					r.tag(AvbPropertyTag::Int32);
					r.u32();
					break;
				}
				case 2:
				case 3:
					r.tag(AvbPropertyTag::Int64);
					r.skip(8);
					break;
				case 4:
					r.tag(AvbPropertyTag::UInt32);
					r.skip(4);
					break;
				case 5:
				case 6:
					r.tag(AvbPropertyTag::Int32);
					r.skip(4);
					break;
				case 8:
					typedMobId(r);
					break;
				case 9:
				{
					r.tag(AvbPropertyTag::UInt32);
					const auto size = r.u32();
					if (size)
					{
						r.tag(AvbPropertyTag::Bytes);
						if (r.u32() != size)
							r.fail(QStringLiteral("Invalid AudioSuite preset length"));
						r.skip(size);
					}
					break;
				}
				default:
					r.unknownExtension(tag);
				}
			}
			r.finish();
		}

		void referenceList(AvbValueParser &r, const QByteArray &type)
		{
			r.start(1);
			const qint64 encodedCount = type == "TMCS" ? static_cast<qint64>(r.s16()) : r.u32();
			const auto entries = r.count(encodedCount, sizeof(quint32));
			for (quint32 i = 0; i < entries; ++i)
				ref(r);
			r.finish();
		}

		void dependencyComponent(AvbValueParser &r, const QByteArray &type)
		{
			component(r);
			if (type == "SEQU")
			{
				r.start(3);
				const auto entries = r.count(r.u32(), 4);
				for (quint32 i = 0; i < entries; ++i)
					ref(r);
				r.finish();
			}
			else
			{
				r.start(kClipVersion);
				r.skip(4);
				// The remaining settings contain no file identities or clip metadata.
				if (type == "PRCL" || type == "CTRL")
					return;
				r.start(1);
				if (type == "TCCP")
					r.skip(16);
				else if (type == "ECCP")
					r.skip(20);
				else if (type == "TRKR")
					r.skip(4);
				r.finish();
			}
		}

		void dependencyTrackGroup(AvbValueParser &r, const QByteArray &type)
		{
			component(r);
			const auto tracks = trackGroup(r);
			if (type == "TRKG")
			{
				r.finish();
				return;
			}
			if (type == "SLCT")
			{
				r.start(1);
				r.skip(1);
				if (r.u16() >= tracks)
					r.fail(QStringLiteral("AVB selector refers to an absent track"));
				r.finish();
				return;
			}
			if (type == "RSET")
			{
				r.start(1);
				quint8 tag{};
				while (r.extension(tag))
				{
					if (tag != 1)
						r.unknownExtension(tag);
					r.tag(AvbPropertyTag::Int32);
					r.skip(4);
				}
				r.finish();
				return;
			}
			if (type == "MASK" || type == "STRB" || type == "SPED" || type == "REPT")
			{
				r.start(2);
				r.skip(4);
				r.start(type == "SPED" ? 3 : 1);
				if (type == "MASK")
					r.skip(5);
				else if (type == "STRB")
					r.skip(4);
				else if (type == "SPED")
				{
					r.skip(8);
					quint8 tag{};
					while (r.extension(tag))
					{
						if (tag == 1)
						{
							r.tag(AvbPropertyTag::Double);
							r.skip(8);
						}
						else if (tag == 2)
						{
							r.tag(AvbPropertyTag::UInt32);
							ref(r);
						}
						else if (tag == 3)
						{
							r.tag(AvbPropertyTag::Boolean);
							r.skip(1);
						}
						else
							r.unknownExtension(tag);
					}
				}
				r.finish();
			}
			// Remaining effect payloads are skipped after their shared prefixes.
			// Indexed objects are visited independently; links inside skipped data are not validated here.
		}

		void parseObject(AvbValueParser &r, const QByteArray &type, quint32 id)
		{
			if (type == "ABIN" || type == "BINF")
				bin(r, type);
			else if (type == "CMPO")
				composition(r, id);
			else if (type == "SCLP")
				sourceClip(r);
			else if (type == "ATTR")
				attributes(r, id);
			else if (type == "MCBR")
				binReference(r, id);
			else if (type == "ASPI")
				audioPlugin(r);
			else if (type == "PRLS" || type == "TMCS")
				referenceList(r, type);
			else if (type == "SEQU" || type == "TCCP" || type == "ECCP" || type == "TRKR" ||
					 type == "FILL" || type == "PRCL" || type == "CTRL")
				dependencyComponent(r, type);
			else if (type == "TRKG" || type == "SLCT" || type == "RSET" || type == "MASK" ||
					 type == "STRB" || type == "SPED" || type == "REPT" || type == "TKFX" ||
					 type == "PVOL" || type == "EQMB" || type == "TNFX" || type == "WARP")
				dependencyTrackGroup(r, type);
			else if (type == "MSML")
				readMediaLocator(r);
			else if (type == "MCMR" || type == "TMBC" || type == "APOS" ||
					 type == "ABOB" || type == "DIDP" || type == "MPGP")
				mobReference(r, type);
			else
			{
				// Known payloads without direct MobIds are skipped; their chunk framing was checked.
				// Indexed objects are visited independently; links inside these payloads are not validated here.
				static const QSet<QByteArray> noIdentityClasses = {
					"ASET", "BVst", "FILE", "WINF", "URLL",
					"GRFX", "SHLP", "CCFX", "FXPS", "AVUP", "PRIT",
					"TKMN", "TKDS", "TKPS", "TKDA", "TKPA",
					"MDES", "MDTP", "MDFM", "MDNG", "MDFL", "MULD",
					"WAVE", "AIFC", "PCMA", "MPGA", "DIDD",
					"CDCI", "MPGI", "JPED", "RGBA", "DATD", "ANCD"};
				if (!noIdentityClasses.contains(type))
					warn(QStringLiteral(
							 "Unsupported AVB class %1; whole-bin identity coverage is incomplete.")
							 .arg(QString::fromLatin1(type)));
			}
		}

		QFile &m_file;
		AvbBin &m_result;
		const std::atomic_bool *m_cancelled;
		bool m_little = true;
		qint64 m_size = 0;
		QDateTime m_modified;
		quint32 m_root = 0;
		QVector<AvbObject> m_objects;
		QHash<quint32, AvbComposition> m_compositions;
		QHash<quint32, quint32> m_originalBinRefs;
		QHash<quint32, AvbBinReference> m_binReferences;
	};
}

AvbHeaderCheck AvbParser::inspectHeader(const QString &avbFilePath)
{
	const QFileInfo fileInfo(avbFilePath);
	if (!fileInfo.exists())
		return {false, QStringLiteral("This bin file no longer exists.")};
	if (!fileInfo.isFile())
		return {false, QStringLiteral("This path is not a regular file.")};
	QFile file(avbFilePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		return {false, file.errorString()};
	std::array<char, kLittleEndianAvbHeader.size()> header{};
	constexpr auto headerBytes = static_cast<qint64>(kLittleEndianAvbHeader.size());
	if (file.read(header.data(), headerBytes) != headerBytes)
		return {false, file.error() == QFileDevice::NoError
						   ? QStringLiteral("This file is not an Avid bin.")
						   : file.errorString()};
	const std::string_view signature(header.data(), header.size());
	if (signature != kLittleEndianAvbHeader && signature != kBigEndianAvbHeader)
		return {false, QStringLiteral("This file is not an Avid bin.")};
	return {true, {}};
}

AvbBin AvbParser::parse(const QString &avbFilePath, const std::atomic_bool *cancelled)
{
	AvbBin result;
	result.filePath = avbFilePath;
	result.displayName = QFileInfo(avbFilePath).completeBaseName();
	if (cancelled && cancelled->load(std::memory_order_relaxed))
	{
		result.error = QStringLiteral("Bin reading cancelled.");
		return result;
	}

	if (!QFileInfo(avbFilePath).isFile())
	{
		result.error = QStringLiteral("AVB path is not a regular file.");
		return result;
	}

	QFile file(avbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		result.error = file.errorString();
		return result;
	}

	try
	{
		AvbFileParser(file, result, cancelled).parse();
	}
	catch (const AvbParserFailure &problem)
	{
		result.valid = false;
		result.complete = false;
		result.mediaFileIds = {};
		result.mobs.clear();
		result.error = problem.message;
		qCWarning(lcAvb) << "cannot parse" << avbFilePath << result.error;
	}

	return result;
}
