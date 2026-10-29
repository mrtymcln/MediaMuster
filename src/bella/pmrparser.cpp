#include "pmrparser.h"

#include "avidtext.h"
#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QtEndian>
#include <utility>
#include <variant>

namespace Bella
{
	namespace
	{
		constexpr quint32 kPmrMagic = 0x000007a9;
		constexpr qint32 kUnicodeVersion = 16;
		constexpr quint16 kNullString = 0xffff;

		// Views never escape the single captured QByteArray's lifetime.
		class Cursor
		{
		public:
			Cursor(QByteArrayView bytes, ByteOrder order, qsizetype offset = 0)
				: m_bytes(bytes), m_order(order), m_offset(offset)
			{
			}

			qsizetype offset() const { return m_offset; }
			qsizetype remaining() const { return m_bytes.size() - m_offset; }
			ByteOrder order() const { return m_order; }
			const QString &error() const { return m_error; }

			std::optional<QByteArrayView> take(qsizetype size, const char *field)
			{
				if (size < 0 || size > remaining())
				{
					m_error = QStringLiteral("%1 at byte %2 needs %3 bytes; only %4 remain.")
								  .arg(QString::fromLatin1(field))
								  .arg(m_offset)
								  .arg(size)
								  .arg(remaining());
					return std::nullopt;
				}
				const auto value = m_bytes.sliced(m_offset, size);
				m_offset += size;
				return value;
			}

			template <typename T>
			std::optional<T> number(const char *field)
			{
				const auto bytes = take(sizeof(T), field);
				if (!bytes)
					return std::nullopt;
				return m_order == ByteOrder::BigEndian ? qFromBigEndian<T>(bytes->data())
													   : qFromLittleEndian<T>(bytes->data());
			}

			std::optional<QByteArrayView> countedString(const char *field)
			{
				const auto size = number<quint16>(field);
				if (!size)
					return std::nullopt;
				if (*size == kNullString)
					return QByteArrayView{};
				return take(*size, field);
			}

		private:
			QByteArrayView m_bytes;
			ByteOrder m_order;
			qsizetype m_offset;
			QString m_error;
		};

		struct WireEntry
		{
			QByteArrayView fileId;
			QByteArrayView filename;
			QByteArrayView project;
			QByteArrayView masterId;
		};

		std::optional<WireEntry> readWireEntry(Cursor &cursor, qint32 version)
		{
			const qsizetype idSize = version <= 7 ? 8 : 32;
			const auto fileId = cursor.take(idSize, "FileMobId");
			if (!fileId)
				return std::nullopt;
			const auto filename = cursor.countedString("Filename");
			if (!filename)
				return std::nullopt;
			WireEntry entry{*fileId, *filename, {}, {}};
			if (version != 1)
			{
				const auto project = cursor.countedString("Project");
				if (!project)
					return std::nullopt;
				const auto masterId = cursor.take(idSize, "MasterMobId");
				if (!masterId)
					return std::nullopt;
				entry.project = *project;
				entry.masterId = *masterId;
			}
			if (!cursor.take(sizeof(quint32), "ignored modification word"))
				return std::nullopt;
			return entry;
		}

		qsizetype minimumRecordSize(qint32 version)
		{
			return version == 1 ? 14 : version <= 7 ? 24
													: 72;
		}

		bool countFits(const Cursor &cursor, qint32 version, quint32 count)
		{
			// Bound work by bytes actually captured, without multiplying an untrusted count.
			return quint64(count) <= quint64(cursor.remaining() / minimumRecordSize(version));
		}

		QString countFailure(const Cursor &cursor, qint32 version, quint32 count)
		{
			return QStringLiteral("Version %1 declares %2 records at byte %3, beyond the %4 remaining bytes.")
				.arg(version)
				.arg(count)
				.arg(cursor.offset())
				.arg(cursor.remaining());
		}

		// Walk only bounded framing: no base text decoding or retained base records.
		std::optional<QString> frameBaseSet(Cursor &cursor, qint32 version, quint32 count)
		{
			if (!countFits(cursor, version, count))
				return countFailure(cursor, version, count);
			for (quint32 index = 0; index < count; ++index)
				if (!readWireEntry(cursor, version))
					return QStringLiteral("Version %1 record %2: %3")
						.arg(version)
						.arg(quint64(index) + 1)
						.arg(cursor.error());
			return std::nullopt;
		}

		QByteArrayView interpretedString(QByteArrayView bytes)
		{
			const auto nul = bytes.indexOf('\0');
			return nul < 0 ? bytes : bytes.first(nul);
		}

		QString decodeMacRoman(QByteArrayView bytes)
		{
			QString value;
			value.reserve(bytes.size());
			for (const auto byte : bytes)
			{
				const auto code = static_cast<unsigned char>(byte);
				value.append(code < 0x80 ? QChar(char16_t(code))
										 : QChar(AvidText::kMacRomanHigh[code - 0x80]));
			}
			return value;
		}

		struct DecodedEntry
		{
			std::optional<PmrEntry> entry;
			PmrReadResult failure = PmrReadResult::Malformed;
			QString explanation;
		};

		DecodedEntry decodeEntry(const WireEntry &wire, qint32 version, ByteOrder order)
		{
			auto nameBytes = wire.filename;
			if (nameBytes.isEmpty())
				return {{}, PmrReadResult::NoUsableSet, QStringLiteral("Filename is empty or null.")};
			QString filename;
			if (version == kUnicodeVersion)
			{
				if (nameBytes.size() < 2 || nameBytes[0] != '\0')
					return {{}, PmrReadResult::Malformed, QStringLiteral("Filename has an invalid v16 prefix.")};
				nameBytes = interpretedString(nameBytes.sliced(2));
				QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
				filename = utf8.decode(nameBytes);
				const bool isValidUtf8Filename = !utf8.hasError();
				if (!isValidUtf8Filename)
					return {{}, PmrReadResult::Malformed, QStringLiteral("Filename is not valid UTF-8.")};
			}
			else
				filename = decodeMacRoman(interpretedString(nameBytes));
			if (filename.isEmpty())
				return {{}, PmrReadResult::NoUsableSet, QStringLiteral("Filename is empty or null.")};

			const auto layout = version <= 7 ? MobIdLayout::PmrLegacy8 : MobIdLayout::Aaf32;
			const auto fileResult = normalizeMobId(wire.fileId, layout, order);
			const auto *fileId = std::get_if<MobId>(&fileResult);
			if (!fileId)
				return {{}, PmrReadResult::Malformed, QStringLiteral("FileMobId has an invalid width.")};
			std::optional<MobId> masterId;
			if (version != 1)
			{
				const auto masterResult = normalizeMobId(wire.masterId, layout, order);
				const auto *id = std::get_if<MobId>(&masterResult);
				if (!id)
					return {{}, PmrReadResult::Malformed, QStringLiteral("MasterMobId has an invalid width.")};
				masterId = *id;
			}
			return {PmrEntry{*fileId, std::move(filename),
							 decodeMacRoman(interpretedString(wire.project)), std::move(masterId)},
					PmrReadResult::Completed,
					{}};
		}

		struct SetResult
		{
			std::optional<PmrSet> knownSet;
			PmrReadResult reading = PmrReadResult::Malformed;
			bool framingComplete = false;
			QString explanation;
		};

		SetResult decodeSet(Cursor &cursor, qint32 version, quint32 count)
		{
			if (!countFits(cursor, version, count))
				return {{}, PmrReadResult::Malformed, false, countFailure(cursor, version, count)};
			SetResult result{PmrSet{version, {}}, PmrReadResult::Completed, false, {}};
			for (quint32 index = 0; index < count; ++index)
			{
				const auto wire = readWireEntry(cursor, version);
				if (!wire)
					return {{}, PmrReadResult::Malformed, false, QStringLiteral("Version %1 record %2: %3").arg(version).arg(quint64(index) + 1).arg(cursor.error())};
				// After content rejection, finish framing to detect unknown outer content.
				// Do not decode or accumulate more entries from the rejected set.
				if (!result.knownSet)
					continue;
				auto decoded = decodeEntry(*wire, version, cursor.order());
				if (!decoded.entry)
				{
					result.knownSet.reset();
					result.reading = decoded.failure;
					result.explanation = QStringLiteral("Version %1 record %2: %3")
											 .arg(version)
											 .arg(quint64(index) + 1)
											 .arg(decoded.explanation);
				}
				else
					result.knownSet->entries.append(std::move(*decoded.entry));
			}
			result.framingComplete = true;
			if (result.knownSet && result.knownSet->entries.isEmpty())
			{
				result.reading = PmrReadResult::NoUsableSet;
				result.explanation = QStringLiteral("Version %1 contains no entries.").arg(version);
			}
			return result;
		}

		void explain(PmrReport &report, const QString &message)
		{
			if (!message.isEmpty())
				report.diagnostics.append(message);
		}

		bool cancelled(PmrReport &report, const std::atomic_bool &stopRequested)
		{
			if (!stopRequested.load(std::memory_order_relaxed))
				return false;
			report.reading = PmrReadResult::Cancelled;
			explain(report, QStringLiteral("PMR reading cancelled at a whole-file or whole-set boundary."));
			return true;
		}

		void selectSet(PmrReport &report, SetResult result, bool completeCoverage, bool unsupported)
		{
			report.reading = unsupported ? PmrReadResult::Unsupported : result.reading;
			report.records.canUsePmrMetadata = !unsupported && result.reading == PmrReadResult::Completed;
			report.records.referenceCoverageComplete = !unsupported && completeCoverage && result.knownSet.has_value();
			report.records.knownSet = std::move(result.knownSet);
			explain(report, result.explanation);
		}

		void parseCapture(QByteArrayView bytes, PmrReport &report, const std::atomic_bool &stopRequested)
		{
			if (bytes.size() < qsizetype(sizeof(quint32)))
			{
				report.reading = PmrReadResult::Malformed;
				explain(report, QStringLiteral("PMR magic is missing or incomplete."));
				return;
			}
			const bool little = qFromLittleEndian<quint32>(bytes.data()) == kPmrMagic;
			if (!little && qFromBigEndian<quint32>(bytes.data()) != kPmrMagic)
			{
				report.reading = PmrReadResult::Malformed;
				explain(report, QStringLiteral("Unrecognised PMR magic."));
				return;
			}
			Cursor base(bytes, little ? ByteOrder::LittleEndian : ByteOrder::BigEndian, sizeof(quint32));
			const auto version = base.number<qint32>("base version");
			const auto count = version ? base.number<quint32>("base record count") : std::nullopt;
			if (!count)
			{
				report.reading = PmrReadResult::Malformed;
				explain(report, base.error());
				return;
			}
			if (*version >= 9)
			{
				report.reading = PmrReadResult::Unsupported;
				explain(report, QStringLiteral("Unsupported PMR base version %1.").arg(*version));
				return;
			}
			if (*version <= 0)
				explain(report, QStringLiteral("Base version %1 follows the existing signed legacy-layout branch; historical format support is unverified.").arg(*version));

			const Cursor baseStart = base;
			if (cancelled(report, stopRequested))
				return;
			const auto framingError = frameBaseSet(base, *version, *count);
			if (framingError)
			{
				report.reading = PmrReadResult::Malformed;
				explain(report, *framingError);
			}
			if (cancelled(report, stopRequested) || framingError)
				return;

			bool unsupported = false;
			bool completeCoverage = true;
			if (base.remaining() != 0)
			{
				const auto extension = base.number<qint32>("extension version");
				unsupported = !extension || *extension != kUnicodeVersion;
				completeCoverage = false;
				if (unsupported)
					explain(report, extension ? QStringLiteral("Unsupported PMR extension version %1.").arg(*extension)
											  : QStringLiteral("Unexplained content after the base set. %1").arg(base.error()));
				else
				{
					const auto unicodeCount = base.number<quint32>("v16 record count");
					if (unicodeCount)
					{
						if (cancelled(report, stopRequested))
							return;
						auto unicode = decodeSet(base, kUnicodeVersion, *unicodeCount);
						unsupported = unicode.framingComplete && base.remaining() != 0;
						if (unsupported)
							explain(report, QStringLiteral("Unexplained content after the declared v16 set."));
						if (unicode.reading == PmrReadResult::Completed)
						{
							selectSet(report, std::move(unicode), true, unsupported);
							cancelled(report, stopRequested);
							return;
						}
						completeCoverage = unicode.knownSet.has_value() && !unsupported;
						explain(report, unicode.explanation);
						// Release all rejected/empty preferred-set workspace before fallback.
						if (cancelled(report, stopRequested))
							return;
					}
					else
						explain(report, base.error());
					explain(report, QStringLiteral("Version 16 is unusable; trying the older set from the same capture."));
				}
			}

			if (cancelled(report, stopRequested))
				return;
			auto fallback = baseStart;
			selectSet(report, decodeSet(fallback, *version, *count), completeCoverage, unsupported);
			cancelled(report, stopRequested);
		}

		PmrStamp observeStamp(const QString &path)
		{
			// A fresh path observation each time: QFileInfo(QFile) is also path-based.
			const QFileInfo info(path);
			if (!info.exists())
				return {};
			const auto size = info.size();
			return {size >= 0 ? std::optional<qint64>(size) : std::nullopt, info.lastModified()};
		}

		bool availableSize(const PmrStamp &stamp)
		{
			return stamp.sizeBytes && *stamp.sizeBytes >= 0;
		}
	}

	PmrCheckResult Detail::comparePmrStamps(const PmrStamp &before, const PmrStamp &after)
	{
		const bool sizesAvailable = availableSize(before) && availableSize(after);
		const bool datesAvailable = before.modified.isValid() && after.modified.isValid();
		if ((sizesAvailable && before.sizeBytes != after.sizeBytes) || (datesAvailable && before.modified != after.modified))
			return PmrCheckResult::Changed;
		return sizesAvailable && datesAvailable ? PmrCheckResult::Verified : PmrCheckResult::Unverified;
	}

	PmrReport parsePmr(const QString &path, const std::atomic_bool &stopRequested)
	{
		PmrReport report;
		report.source.path = path;
		if (cancelled(report, stopRequested))
			return report;
		QFile source(path);
		if (!source.open(QIODevice::ReadOnly))
		{
			report.reading = PmrReadResult::IoError;
			explain(report, QStringLiteral("Cannot open PMR: %1").arg(source.errorString()));
			return report;
		}
		report.source.capturedStamp = observeStamp(path);
		if (cancelled(report, stopRequested))
			return report;
		const QByteArray capture = source.readAll();
		const bool readFailed = source.error() != QFileDevice::NoError;
		if (readFailed)
			explain(report, QStringLiteral("Cannot read complete PMR: %1").arg(source.errorString()));
		// No extra filesystem observations merely for diagnostics after noticing Cancel.
		if (cancelled(report, stopRequested))
			return report;
		const auto after = observeStamp(path);
		source.close();
		report.sizeDateCheck = Detail::comparePmrStamps(report.source.capturedStamp, after);
		if (report.sizeDateCheck == PmrCheckResult::Changed)
		{
			report.reading = PmrReadResult::Changed;
			if (availableSize(report.source.capturedStamp) && availableSize(after) && report.source.capturedStamp.sizeBytes != after.sizeBytes)
				explain(report, QStringLiteral("PMR size changed during capture from %1 to %2 bytes; its data is excluded.")
									.arg(*report.source.capturedStamp.sizeBytes)
									.arg(*after.sizeBytes));
			if (report.source.capturedStamp.modified.isValid() && after.modified.isValid() && report.source.capturedStamp.modified != after.modified)
				explain(report, QStringLiteral("PMR modification time changed during capture from %1 to %2; its data is excluded.")
									.arg(report.source.capturedStamp.modified.toUTC().toString(Qt::ISODateWithMs),
										 after.modified.toUTC().toString(Qt::ISODateWithMs)));
			return report;
		}
		if (report.sizeDateCheck == PmrCheckResult::Unverified)
		{
			if (!availableSize(report.source.capturedStamp) || !availableSize(after))
				explain(report, QStringLiteral("Size check unavailable before or after capture; otherwise usable metadata remains unverified."));
			if (!report.source.capturedStamp.modified.isValid() || !after.modified.isValid())
				explain(report, QStringLiteral("Modification-time check unavailable before or after capture; otherwise usable metadata remains unverified."));
		}
		const bool wrongSize = (availableSize(report.source.capturedStamp) && capture.size() != *report.source.capturedStamp.sizeBytes) || (availableSize(after) && capture.size() != *after.sizeBytes);
		if (readFailed || wrongSize)
		{
			report.reading = PmrReadResult::IoError;
			if (wrongSize)
				explain(report, QStringLiteral("Captured %1 bytes, which does not match the available expected PMR size.").arg(capture.size()));
			return report;
		}
		parseCapture(capture, report, stopRequested);
		return report;
	}
}
