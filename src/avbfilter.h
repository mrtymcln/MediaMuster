#pragma once

#include "avbfilereferences.h"

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QSharedPointer>
#include <QStringList>

namespace MediaEngine
{
	struct ParsedSource;
	struct AvbReferenceResult;
}

/// Ordered operations on files identified by the selected bins' MSML locators.
/// A shared master MobId does not establish bin-filter membership.
struct AvbFilter
{
	enum class Operation
	{
		Intersect,
		Subtract,
		Add
	};

	struct Step
	{
		Operation operation = Operation::Intersect;
		QVector<QString> binDisplayNames;
		AvbFileReferences fileReferences;
		QStringList warnings;
		QVector<QSharedPointer<const MediaEngine::ParsedSource>> sourceGraphs;
		QVector<QSharedPointer<const MediaEngine::AvbReferenceResult>> referenceResults;
	};

	QVector<Step> steps;

	[[nodiscard]] bool isActive() const noexcept { return !steps.isEmpty(); }
	[[nodiscard]] bool resultsMayBeIncomplete() const noexcept
	{
		for (const auto &step : steps)
			if (!step.warnings.isEmpty())
				return true;
		return false;
	}

	/// Labels do not affect row membership; operation order and both ID sets do.
	[[nodiscard]] bool hasSameCriteria(const AvbFilter &other) const
	{
		if (steps.size() != other.steps.size())
			return false;
		for (qsizetype i = 0; i < steps.size(); ++i)
		{
			const auto &a = steps[i];
			const auto &b = other.steps[i];
			if (a.operation != b.operation || a.fileReferences.fullIds != b.fileReferences.fullIds ||
				a.fileReferences.legacyKeys != b.fileReferences.legacyKeys)
				return false;
		}
		return true;
	}

	[[nodiscard]] bool matches(const QString &fileMobId) const
	{
		if (steps.isEmpty())
			return true;

		// A leading Subtract removes matches from all media rows, including
		// rows outside every loaded bin. A leading Add starts with its own
		// matches. This remains stable when earlier steps or bins are removed.
		const auto fileId = AvbFileId::fromMobId(fileMobId);
		bool accepted = true;
		bool started = false;
		for (const Step &step : steps)
		{
			// An operand without usable file identities leaves the result unchanged.
			if (step.fileReferences.isEmpty())
				continue;
			if (!started)
			{
				accepted = step.operation != Operation::Add;
				started = true;
			}
			const bool hit = step.fileReferences.matches(fileId);
			switch (step.operation)
			{
			case Operation::Intersect:
				accepted = accepted && hit;
				break;
			case Operation::Subtract:
				accepted = accepted && !hit;
				break;
			case Operation::Add:
				accepted = accepted || hit;
				break;
			}
		}
		return accepted;
	}
};

Q_DECLARE_METATYPE(AvbFilter)
