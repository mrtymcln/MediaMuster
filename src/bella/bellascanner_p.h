#pragma once

#include "bellascanner.h"

namespace Bella::Detail
{
	enum class PhysicalPresence
	{
		Unconfirmed,
		File,
		Replaced
	};

	struct PhysicalObservation
	{
		PhysicalPresence presence = PhysicalPresence::Unconfirmed;
		std::optional<qint64> sizeBytes;
		QDateTime modified;
	};

	enum class PhysicalCheck
	{
		Verified,
		Unverified,
		Changed,
		Unavailable
	};

	[[nodiscard]] PhysicalCheck comparePhysical(const ScanEntry &entry,
												const PhysicalObservation &observed);
}
