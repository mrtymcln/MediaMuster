#pragma once

#include <QDateTime>
#include <optional>

namespace Bella::Detail
{
	struct FileStamp
	{
		std::optional<qint64> sizeBytes;
		QDateTime modified;
	};

	struct FileStampComparison
	{
		bool canCompareSize = false;
		bool canCompareModificationTime = false;
		bool sizeChanged = false;
		bool modificationTimeChanged = false;

		[[nodiscard]] bool hasChanged() const
		{
			return sizeChanged || modificationTimeChanged;
		}

		[[nodiscard]] bool isVerified() const
		{
			return canCompareSize && canCompareModificationTime && !hasChanged();
		}
	};

	[[nodiscard]] inline FileStampComparison compareFileStamps(const FileStamp &before, const FileStamp &after)
	{
		const bool canCompareSize = before.sizeBytes && after.sizeBytes && *before.sizeBytes >= 0 && *after.sizeBytes >= 0;
		const bool canCompareModificationTime = before.modified.isValid() && after.modified.isValid();
		return {canCompareSize, canCompareModificationTime,
				canCompareSize && before.sizeBytes != after.sizeBytes,
				canCompareModificationTime && before.modified != after.modified};
	}
}
