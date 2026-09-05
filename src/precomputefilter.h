#pragma once

#include "mediafile.h"

#include <QString>
#include <QVector>

#include <algorithm>

/// One checked branch in the precompute outline. Empty fields mean every
/// descendant at that level; the displayed word "unknown" is an exact value.
/// Retaining the complete path keeps an effect in one branch from selecting
/// an identically named effect in another branch.
struct PrecomputeFilterPath
{
	QString precomputeCategory;
	QString effectCategory;
	QString effect;

	bool operator==(const PrecomputeFilterPath &other) const
	{
		return precomputeCategory == other.precomputeCategory &&
			effectCategory == other.effectCategory && effect == other.effect;
	}

	bool matches(const MediaFile &file) const
	{
		return file.type == MediaFile::Type::Precompute &&
			(precomputeCategory.isEmpty() || precomputeCategory == file.precomputeCategoryDisplay()) &&
			(effectCategory.isEmpty() || effectCategory == file.effectCategoryDisplay()) &&
			(effect.isEmpty() || effect == file.effectDisplay());
	}
};

/// Checked branches are alternatives: a row may match any complete path.
/// An active filter with no checked branches deliberately matches no files;
/// it must not silently become an unrestricted table.
struct PrecomputeFilter
{
	bool active = false;
	QVector<PrecomputeFilterPath> paths;

	bool operator==(const PrecomputeFilter &other) const
	{
		return active == other.active && paths == other.paths;
	}

	bool matches(const MediaFile &file) const
	{
		return !active || std::any_of(paths.cbegin(), paths.cend(),
			[&file](const PrecomputeFilterPath &path) { return path.matches(file); });
	}
};
