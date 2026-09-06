#pragma once

#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVector>

/// Ordered operations on media-row membership. Each operand matches either
/// identity on a row before the operations are combined; reducing the IDs
/// themselves would lose rows whose file and master match different bins.
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
		QSet<QString> mobIds;
	};

	QVector<Step> steps;

	[[nodiscard]] bool isActive() const noexcept { return !steps.isEmpty(); }

	[[nodiscard]] bool matches(const QString &fileMob, const QString &masterMob) const
	{
		if (steps.isEmpty())
			return true;

		// A leading Subtract removes matches from all media rows, including
		// rows outside every loaded bin. A leading Add starts with its own
		// matches. This remains stable when earlier steps or bins are removed.
		bool accepted = steps.first().op != Operation::Add;
		for (const Step &step : steps)
		{
			const bool hit = (!fileMob.isEmpty() && step.mobIds.contains(fileMob)) ||
							 (!masterMob.isEmpty() && step.mobIds.contains(masterMob));
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
