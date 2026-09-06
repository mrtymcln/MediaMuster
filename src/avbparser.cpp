#include "avbparser.h"
#include "avidtext.h"
#include "logcategories.h"
#include "mobid.h"
#include "omfuid.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>

// AVB has ordinal references in a flat chunk stream. An offset table permits
// bounded property reads without loading the file into memory. This reader
// inventories whole-bin identities; it does not evaluate sequence dependencies
// or interpret effect/essence payloads.
namespace
{
	using namespace std::string_view_literals;
	constexpr auto kLittleEndianAvbHeader = "\x06\x00"
											"DomainDJBO\x07\x00"
											"AObjDoc"sv;
	constexpr auto kBigEndianAvbHeader = "\x00\x06"
										 "DomainOBJD\x00\x07"
										 "AObjDoc"sv;
	static_assert(kLittleEndianAvbHeader.size() == kBigEndianAvbHeader.size());
	constexpr qint64 kMaxBinBytes = 256LL * 1024 * 1024;
	constexpr quint32 kMaxObjects = 1'000'000;
	constexpr quint32 kMaxEntries = 1'000'000;
	constexpr int kMaxWarnings = 32;
	// Conservative accounting for retained container nodes/strings, in addition to the
	// file-size bound. Skipped blobs and temporary strings do not accumulate.
	constexpr qint64 kMaxRetainedBytes = 192LL * 1024 * 1024;
	// These estimates include container/node allocation overhead, MOB strings,
	// and the final metadata vector. Variable name storage is charged separately.
	constexpr qint64 kObjectIndexEstimate = 96;
	constexpr qint64 kMobAliasEstimate = 256;
	constexpr qint64 kCompositionEstimate = 384;
	constexpr qint64 kAttributeRefEstimate = 64;
	constexpr qint64 kBinRefEstimate = 192;
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
	enum class PropertyTag : quint8
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
	using RawMob = std::array<uchar, MobId::kRawSize>;
	struct ParseFailure
	{
		QString message;
	};
	struct Unsupported
	{
		QString message;
	};

	// Each cursor owns a byte range. Every read and skip checks that range and
	// actual read success, including if the file is shortened during parsing.
	class Reader
	{
	public:
		Reader(QFile &file, qint64 begin, qint64 end, bool little, const std::atomic_bool *cancelled)
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
			throw ParseFailure{QStringLiteral("%1 (byte %2)").arg(reason).arg(m_pos)};
		}
		[[noreturn]] void unsupported(const QString &reason) const
		{
			throw Unsupported{QStringLiteral("%1 (byte %2)").arg(reason).arg(m_pos)};
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
			if (size < 0 || size > remaining() || size > kNullStringLength)
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
			// Seeking alone can succeed past EOF; check the skipped endpoint.
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
		void tag(PropertyTag expected)
		{
			tag(static_cast<quint8>(expected));
		}
		void start(quint8 version)
		{
			tag(PropertyTag::Class);
			const auto actual = u8();
			if (actual != version)
				unsupported(QStringLiteral("Unsupported AVB object version %1 (expected %2)")
								.arg(actual)
								.arg(version));
		}
		bool extension(quint8 &value)
		{
			if (peek() != static_cast<quint8>(PropertyTag::Extension))
				return false;
			u8();
			value = u8();
			return true;
		}
		[[noreturn]] void unknownExtension(quint8 value) const
		{
			unsupported(QStringLiteral("Unsupported AVB extension 0x%1").arg(value, 2, 16, QLatin1Char('0')));
		}
		void finish()
		{
			tag(PropertyTag::End);
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
				decoded.append(
					b < 0x80 ? QChar(static_cast<char16_t>(b)) : QChar(AvidText::kMacRomanHigh[b - 0x80]));
			}
			return decoded;
		}
		quint32 count(qint64 value, quint32 minimumBytes)
		{
			if (value < 0 || value > kMaxEntries || value > remaining() / minimumBytes)
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

	struct Object
	{
		QByteArray type;
		qint64 offset = 0;
		quint32 size = 0;
	};
	struct BinReference
	{
		QString name;
		QString uid;
	};
	struct Component
	{
		QString name;
		quint32 attributes = 0;
	};
	struct Composition
	{
		AvbMob mob;
		quint32 attributes = 0;
	};

	RawMob nativeMob(quint32 low, quint32 high)
	{
		std::array<uchar, OmfUid::kPmrSize> core{};
		qToLittleEndian(low, core.data());
		qToLittleEndian(high, core.data() + sizeof(low));
		return OmfUid::wrap8(core.data());
	}
	RawMob typedMob(Reader &r)
	{
		RawMob out{};
		r.tag(PropertyTag::Bytes);
		if (r.u32() != kMobLabelBytes)
			r.fail(QStringLiteral("Invalid MOB label length"));
		r.read(reinterpret_cast<char *>(out.data()), kMobLabelBytes);
		for (int i = kMobLabelBytes; i < kMobMaterialOffset; ++i)
		{
			r.tag(PropertyTag::UInt8);
			out[i] = r.u8();
		}
		r.tag(PropertyTag::UInt32);
		qToLittleEndian(r.u32(), out.data() + kMobMaterialOffset);
		r.tag(PropertyTag::UInt16);
		qToLittleEndian(r.u16(), out.data() + kMobMaterialOffset + sizeof(quint32));
		r.tag(PropertyTag::UInt16);
		qToLittleEndian(r.u16(), out.data() + kMobMaterialOffset + sizeof(quint32) + sizeof(quint16));
		r.tag(PropertyTag::Bytes);
		if (r.u32() != kMobTailBytes)
			r.fail(QStringLiteral("Invalid MOB material length"));
		r.read(reinterpret_cast<char *>(out.data() + kMobTailOffset), kMobTailBytes);
		return out;
	}
	bool isNullMob(const RawMob &mob) noexcept
	{
		if (std::all_of(mob.begin(), mob.end(), [](uchar b)
						{ return b == 0; }))
			return true;
		return std::memcmp(mob.data(), OmfUid::kPrefix, sizeof OmfUid::kPrefix) == 0 && std::memcmp(mob.data() + kMobTailOffset, OmfUid::kSuffix, sizeof OmfUid::kSuffix) == 0 && std::all_of(mob.begin() + kMobMaterialOffset, mob.begin() + kMobTailOffset, [](uchar b)
																																															  { return b == 0; });
	}

	class Document
	{
	public:
		Document(QFile &file, AvbBin &result, const std::atomic_bool *cancelled)
			: m_file(file), m_result(result), m_cancelled(cancelled)
		{
		}
		void parse()
		{
			index();
			m_result.complete = true;
			for (quint32 id = 1; id < static_cast<quint32>(m_objects.size()); ++id)
			{
				const auto &object = m_objects[id];
				Reader r(m_file, object.offset, object.offset + object.size, m_little, m_cancelled);
				r.checkCancelled();
				try
				{
					parseObject(r, object.type, id);
				}
				catch (const Unsupported &problem)
				{
					warn(QStringLiteral("Object %1 (%2): %3")
							 .arg(id)
							 .arg(QString::fromLatin1(object.type), problem.message));
				}
			}
			for (auto it = m_compositions.cbegin(); it != m_compositions.cend(); ++it)
			{
				if (m_cancelled && m_cancelled->load(std::memory_order_relaxed))
					throw ParseFailure{QStringLiteral("Bin reading cancelled.")};
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
			std::sort(m_result.mobs.begin(), m_result.mobs.end(),
					  [this](const auto &a, const auto &b)
					  {
						  if (m_cancelled && m_cancelled->load(std::memory_order_relaxed))
							  throw ParseFailure{QStringLiteral("Bin reading cancelled.")};
						  if (a.mobId != b.mobId)
							  return a.mobId < b.mobId;
						  if (a.name != b.name)
							  return a.name < b.name;
						  if (a.originalBinUid != b.originalBinUid)
							  return a.originalBinUid < b.originalBinUid;
						  return a.originalBin < b.originalBin;
					  });
			if (m_cancelled && m_cancelled->load(std::memory_order_relaxed))
				throw ParseFailure{QStringLiteral("Bin reading cancelled.")};
			if (m_file.size() != m_size || m_file.fileTime(QFileDevice::FileModificationTime) != m_modified)
				throw ParseFailure{QStringLiteral("AVB file changed while reading; load it again.")};
			m_result.valid = true;
		}

	private:
		void retain(qint64 bytes)
		{
			if (bytes < 0 || bytes > kMaxRetainedBytes - m_retainedBytes)
				throw ParseFailure{
					QStringLiteral("AVB identity and metadata inventory exceeds the 192 MiB memory budget.")};
			m_retainedBytes += bytes;
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
			if (m_size < 2 || m_size > kMaxBinBytes)
				throw ParseFailure{
					QStringLiteral("AVB file is empty, truncated, or exceeds the 256 MiB limit.")};
			const auto order = m_file.read(2);
			if (order == QByteArray::fromHex("0600"))
				m_little = true;
			else if (order == QByteArray::fromHex("0006"))
				m_little = false;
			else
				throw ParseFailure{QStringLiteral("Not an Avid bin: invalid byte-order marker.")};
			Reader r(m_file, 2, m_size, m_little, m_cancelled);
			if (r.bytes(6) != "Domain" || r.fourcc() != "OBJD" || r.string() != QStringLiteral("AObjDoc"))
				r.fail(QStringLiteral("Not an Avid bin: invalid document header"));
			r.tag(kDocumentVersion);
			r.string();
			const auto count = r.u32();
			m_root = r.u32();
			if (!count || count > kMaxObjects || count > static_cast<quint64>(r.remaining()) / 9 || !m_root || m_root > count)
				r.fail(QStringLiteral("Invalid AVB object count or root reference"));
			if (r.u32() != (m_little ? 0x49494949U : 0x4d4d4d4dU))
				r.fail(QStringLiteral("AVB header byte order is inconsistent"));
			r.skip(8); // Numeric last-save time and reserved word.
			if (r.fourcc() != "ATob" || r.fourcc() != "ATve")
				r.fail(QStringLiteral("Invalid AVB document format identifiers"));
			r.string();
			r.skip(16);
			retain((static_cast<qint64>(count) + 1) * kObjectIndexEstimate);
			m_objects.reserve(count + 1);
			m_objects.append(Object{}); // Index zero is the null reference.
			for (quint32 id = 1; id <= count; ++id)
			{
				const auto type = r.fourcc();
				const auto size = r.u32();
				if (!size || size > r.remaining())
					r.fail(QStringLiteral("Invalid AVB chunk length"));
				if (std::any_of(type.begin(), type.end(),
								[](char c)
								{ return static_cast<quint8>(c) < 32 || static_cast<quint8>(c) > 126; }))
					r.fail(QStringLiteral("Invalid AVB class identifier"));
				m_objects.append({type, r.pos(), size});
				r.skip(size - 1);
				r.tag(PropertyTag::End);
			}
			if (r.remaining())
				r.fail(QStringLiteral("Unexpected data after declared AVB objects"));
			if (m_objects[m_root].type != "ABIN" && m_objects[m_root].type != "BINF")
				r.fail(QStringLiteral("AVB document root is not a bin"));
		}
		quint32 ref(Reader &r, const char *expected = nullptr, bool required = false)
		{
			const auto value = r.u32();
			if (value >= static_cast<quint32>(m_objects.size()) || (required && !value))
				r.fail(QStringLiteral("Invalid AVB object reference %1").arg(value));
			if (value && expected && m_objects[value].type != expected)
				r.fail(QStringLiteral("AVB reference %1 must identify %2")
						   .arg(value)
						   .arg(QString::fromLatin1(expected)));
			return value;
		}
		QString addMob(const RawMob &mob)
		{
			if (isNullMob(mob))
				return {};
			const auto canonical = MobId::format(mob.data());
			if (!m_result.mobIds.contains(canonical))
			{
				retain(kMobAliasEstimate);
				m_result.mobIds.insert(canonical);
			}
			RawMob swapped{};
			MobId::swapMiddleFields(mob.data(), swapped.data());
			const auto alias = MobId::format(swapped.data());
			if (!m_result.mobIds.contains(alias))
			{
				retain(kMobAliasEstimate);
				m_result.mobIds.insert(alias);
			}
			return canonical;
		}
		Component component(Reader &r)
		{
			r.start(kComponentVersion);
			ref(r);
			ref(r);
			r.skip(8);
			Component result;
			result.name = r.string();
			r.string(); // Effect identifier is text, never MOB identity evidence.
			result.attributes = ref(r, "ATTR");
			ref(r);
			ref(r);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				r.tag(PropertyTag::UInt32);
				ref(r);
			}
			return result;
		}
		quint32 trackGroup(Reader &r)
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
					r.tag(PropertyTag::Int16);
					r.skip(2);
				}
			}
			return tracks;
		}
		void composition(Reader &r, quint32 id)
		{
			Composition value;
			const auto base = component(r);
			value.attributes = base.attributes;
			value.mob.name = base.name;
			trackGroup(r);
			r.start(kCompositionVersion);
			const auto low = r.u32();
			const auto high = r.u32();
			auto mob = nativeMob(low, high);
			r.skip(4);
			value.mob.mobType = r.u8();
			value.mob.usageCode = r.s32();
			ref(r);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag == 1)
				{
					r.tag(PropertyTag::Int32);
					r.skip(4);
				}
				else if (tag == 2)
					mob = typedMob(r);
				else
					r.unknownExtension(tag);
			}
			r.finish();
			value.mob.mobId = addMob(mob);
			if (!value.mob.mobId.isEmpty())
			{
				retain(kCompositionEstimate + value.mob.name.size() * sizeof(QChar));
				m_compositions.insert(id, std::move(value));
			}
		}
		void sourceClip(Reader &r)
		{
			component(r);
			r.start(kClipVersion);
			r.skip(4);
			r.start(kSourceClipVersion);
			const auto low = r.u32();
			const auto high = r.u32();
			auto mob = nativeMob(low, high);
			r.skip(6);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				mob = typedMob(r);
			}
			r.finish();
			addMob(mob);
		}
		void bin(Reader &r, const QByteArray &type)
		{
			r.tag(PropertyTag::Class);
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
		void attributes(Reader &r, quint32 id)
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
			{
				retain(kAttributeRefEstimate);
				m_originalBinRefs.insert(id, originalBin);
			}
		}
		void binReference(Reader &r, quint32 id)
		{
			r.start(1);
			const auto high = r.u32();
			const auto low = r.u32();
			BinReference value;
			value.uid = QStringLiteral("%1%2").arg(high, 8, 16, QLatin1Char('0')).arg(low, 8, 16, QLatin1Char('0'));
			value.name = r.string();
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 1)
					r.unknownExtension(tag);
				r.tag(PropertyTag::String);
				const auto utf8 = r.string(true);
				if (!utf8.isEmpty())
					value.name = utf8;
			}
			r.finish();
			retain(kBinRefEstimate + value.name.size() * sizeof(QChar));
			m_binReferences.insert(id, std::move(value));
		}
		void mobReference(Reader &r, const QByteArray &type)
		{
			r.start(type == "MSML" ? 2 : 1);
			const auto low = r.u32();
			const auto high = r.u32();
			auto mob = nativeMob(low, high);
			if (type == "MSML")
				r.string();
			else if (type == "MCMR" || type == "TMBC")
				r.skip(4);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (type == "MSML")
				{
					if (tag == 1)
					{
						r.tag(PropertyTag::Int32);
						r.skip(4);
					}
					else if (tag == 2)
						mob = typedMob(r);
					else if (tag == 3)
					{
						r.tag(PropertyTag::String);
						r.string(true);
					}
					else
						r.unknownExtension(tag);
				}
				else
				{
					if (tag != 1)
						r.unknownExtension(tag);
					mob = typedMob(r);
				}
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
					r.tag(PropertyTag::Boolean);
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
			addMob(mob);
		}
		void audioPlugin(Reader &r)
		{
			component(r);
			trackGroup(r);
			r.start(6); // TrackEffect base.
			r.skip(24);
			ref(r);
			r.skip(2);
			quint8 tag{};
			while (r.extension(tag))
			{
				if (tag != 2)
					r.unknownExtension(tag);
				r.tag(PropertyTag::UInt32);
				ref(r);
			}
			r.start(1);
			// The supplied reference and specimens establish one plug-in.
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
			RawMob mob{};
			bool fullMob = false;
			while (r.extension(tag))
			{
				switch (tag)
				{
				case 1:
				{
					r.tag(PropertyTag::Int32);
					const auto low = r.u32();
					r.tag(PropertyTag::Int32);
					const auto high = r.u32();
					if (!fullMob)
						mob = nativeMob(low, high);
					break;
				}
				case 2:
				case 3:
					r.tag(PropertyTag::Int64);
					r.skip(8);
					break;
				case 4:
					r.tag(PropertyTag::UInt32);
					r.skip(4);
					break;
				case 5:
				case 6:
					r.tag(PropertyTag::Int32);
					r.skip(4);
					break;
				case 8:
					mob = typedMob(r);
					fullMob = true;
					break;
				case 9:
				{
					r.tag(PropertyTag::UInt32);
					const auto size = r.u32();
					if (size)
					{
						r.tag(PropertyTag::Bytes);
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
			addMob(mob);
		}
		void referenceList(Reader &r, const QByteArray &type)
		{
			r.start(1);
			const qint64 encodedCount = type == "TMCS" ? static_cast<qint64>(r.s16()) : r.u32();
			const auto entries = r.count(encodedCount, sizeof(quint32));
			for (quint32 i = 0; i < entries; ++i)
				ref(r);
			r.finish();
		}
		void dependencyComponent(Reader &r, const QByteArray &type)
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
				r.skip(4); // Clip base and length.
				if (type == "PRCL" || type == "CTRL")
					return; // Parameter/control payload; common references validated.
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
		void dependencyTrackGroup(Reader &r, const QByteArray &type)
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
					r.tag(PropertyTag::Int32);
					r.skip(4);
				}
				r.finish();
				return;
			}
			if (type == "MASK" || type == "STRB" || type == "SPED" || type == "REPT")
			{
				r.start(2);
				r.skip(4); // TimeWarp base.
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
							r.tag(PropertyTag::Double);
							r.skip(8);
						}
						else if (tag == 2)
						{
							r.tag(PropertyTag::UInt32);
							ref(r);
						}
						else if (tag == 3)
						{
							r.tag(PropertyTag::Boolean);
							r.skip(1);
						}
						else
							r.unknownExtension(tag);
					}
				}
				r.finish();
			}
			// Other effect subclasses have no direct MOB field. Their shared
			// component/track references are checked; effect payloads are skipped.
		}
		void parseObject(Reader &r, const QByteArray &type, quint32 id)
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
			else if (type == "SEQU" || type == "TCCP" || type == "ECCP" || type == "TRKR" || type == "FILL" || type == "PRCL" || type == "CTRL")
				dependencyComponent(r, type);
			else if (type == "TRKG" || type == "SLCT" || type == "RSET" || type == "MASK" || type == "STRB" || type == "SPED" || type == "REPT" || type == "TKFX" || type == "PVOL" || type == "EQMB" || type == "TNFX" || type == "WARP")
				dependencyTrackGroup(r, type);
			else if (type == "MCMR" || type == "TMBC" || type == "MSML" || type == "APOS" || type == "ABOB" || type == "DIDP" || type == "MPGP")
				mobReference(r, type);
			else
			{
				// Known classes with no direct MOB property. Referred objects are
				// inventoried separately; timeline semantics and opaque payloads
				// are outside this whole-bin identity reader.
				static const QSet<QByteArray> noIdentityClasses = {"ASET", "BVst", "FILE", "WINF", "URLL",
																   "GRFX", "SHLP", "CCFX", "FXPS", "AVUP", "PRIT", "TKMN", "TKDS", "TKPS", "TKDA", "TKPA",
																   "MDES", "MDTP", "MDFM", "MDNG", "MDFL", "MULD", "WAVE", "AIFC", "PCMA", "MPGA", "DIDD",
																   "CDCI", "MPGI", "JPED", "RGBA", "DATD", "ANCD"};
				if (!noIdentityClasses.contains(type))
					warn(
						QStringLiteral("Unsupported AVB class %1; whole-bin identity coverage is incomplete.")
							.arg(QString::fromLatin1(type)));
			}
		}
		QFile &m_file;
		AvbBin &m_result;
		const std::atomic_bool *m_cancelled;
		bool m_little = true;
		qint64 m_size = 0;
		qint64 m_retainedBytes = 0;
		QDateTime m_modified;
		quint32 m_root = 0;
		QVector<Object> m_objects;
		QHash<quint32, Composition> m_compositions;
		QHash<quint32, quint32> m_originalBinRefs;
		QHash<quint32, BinReference> m_binReferences;
	};
} // namespace

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
		Document(file, result, cancelled).parse();
	}
	catch (const ParseFailure &problem)
	{
		result.valid = false;
		result.complete = false;
		result.mobIds.clear();
		result.mobs.clear();
		result.error = problem.message;
		qCWarning(lcAvb) << "cannot parse" << avbFilePath << result.error;
	}
	return result;
}
