// Captures a database without changing its bytes or interpreting its records.
// Each bounded read writes into the owned image, with no intermediate copy.

#include "databaseimage.h"

#include <algorithm>
#include <limits>

namespace MediaEngine
{
	DatabaseImage DatabaseImage::read(QIODevice &source, const MediaEngine::Cancellation &cancellation)
	{
		using Outcome = MediaEngine::ParsedSource::Outcome;
		constexpr qint64 readBlockSize = 1024 * 1024;
		DatabaseImage result;
		const auto fail = [&result](Outcome outcome, const QString &diagnostic)
		{
			result.m_outcome = outcome;
			result.m_diagnostics.append(diagnostic);
		};
		const auto cancelled = [&cancellation, &fail]()
		{
			if (!cancellation.cancelled())
				return false;
			fail(Outcome::Cancelled, QStringLiteral("Database image capture cancelled."));
			return true;
		};

		if (cancelled())
			return result;
		if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
		{
			fail(Outcome::IoError, QStringLiteral("Database input must be open, readable, seekable and binary."));
			return result;
		}
		const qint64 initialSize = source.size();
		if (initialSize < 0)
		{
			fail(Outcome::IoError, QStringLiteral("Cannot determine database extent: %1").arg(source.errorString()));
			return result;
		}
		result.m_expectedSize = initialSize;
		if (!source.seek(0))
		{
			fail(Outcome::IoError, QStringLiteral("Cannot seek to database start: %1").arg(source.errorString()));
			return result;
		}
		// QByteArray indexes use qsizetype and its allocation includes a terminator.
		// This checks representability, without imposing a database size policy.
		constexpr qint64 maxImageSize = static_cast<qint64>(std::numeric_limits<qsizetype>::max() - 1);
		if (result.m_expectedSize > maxImageSize)
		{
			fail(Outcome::IoError, QStringLiteral("Database extent cannot be represented by a RAM image on this platform."));
			return result;
		}
		if (cancelled())
			return result;

		result.m_bytes.reserve(static_cast<qsizetype>(result.m_expectedSize));
		qint64 acquiredSize = 0;
		while (acquiredSize < result.m_expectedSize)
		{
			if (cancelled())
				return result;
			const qint64 requested = std::min(readBlockSize, result.m_expectedSize - acquiredSize);
			result.m_bytes.resize(static_cast<qsizetype>(acquiredSize + requested));
			const qint64 received = source.read(result.m_bytes.data() + static_cast<qsizetype>(acquiredSize), requested);
			if (received <= 0)
			{
				result.m_bytes.resize(static_cast<qsizetype>(acquiredSize));
				const auto outcome = received == 0 && source.atEnd() ? Outcome::Incomplete : Outcome::IoError;
				fail(outcome, QStringLiteral("Cannot capture database at byte %1 of %2: %3")
								  .arg(acquiredSize)
								  .arg(result.m_expectedSize)
								  .arg(source.errorString()));
				return result;
			}
			acquiredSize += received;
			result.m_bytes.resize(static_cast<qsizetype>(acquiredSize));
		}
		if (cancelled())
			return result; // Also covers cancellation during the final read.
		const qint64 finalSize = source.size();
		if (finalSize < 0)
		{
			fail(Outcome::IoError, QStringLiteral("Cannot verify database extent after capture: %1").arg(source.errorString()));
			return result;
		}
		if (finalSize != result.m_expectedSize)
		{
			fail(Outcome::Incomplete, QStringLiteral("Database length changed during capture; source must be checked again."));
			return result;
		}
		if (cancelled())
			return result;
		result.m_outcome = Outcome::Complete;
		return result;
	}
}
