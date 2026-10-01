#pragma once

#include "binmediaids.h"

#include <QMetaType>
#include <QString>
#include <QVector>

/// Ordered operations on files identified by the selected bins' MSML locators.
/// A shared master MobId does not establish bin-filter membership.
struct BinFilter
{
	enum class Operation
	{
		Intersect,
		Subtract,
		Add
	};

	struct Step
	{
		Operation op = Operation::Intersect;
		QVector<QString> binDisplayNames;
		BinMediaIds mediaFileIds;
	};

	QVector<Step> steps;

	[[nodiscard]] bool isActive() const noexcept { return !steps.isEmpty(); }

	[[nodiscard]] bool matches(const QString &fileMob) const
	{
		if (steps.isEmpty())
			return true;

		// A leading Subtract removes matches from all media rows, including
		// rows outside every loaded bin. A leading Add starts with its own
		// matches. This remains stable when earlier steps or bins are removed.
		const auto fileId = BinMediaId::fromMobId(fileMob);
		bool accepted = true;
		bool started = false;
		for (const Step &step : steps)
		{
			// An operand without usable file identities leaves the result unchanged.
			if (step.mediaFileIds.isEmpty())
				continue;
			if (!started)
			{
				accepted = step.op != Operation::Add;
				started = true;
			}
			const bool hit = step.mediaFileIds.contains(fileId);
			switch (step.op)
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

Q_DECLARE_METATYPE(BinFilter)
