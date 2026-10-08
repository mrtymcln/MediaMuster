#pragma once

// Shared display preferences operate on retained, qualified evidence. Source
// ownership, freshness and format interpretation are established before selection.
#include "mediaevidence.h"

#include <array>
#include <cstddef>

namespace Canon
{
	// Resolver storage, built from the named prefer() groups in the policy table.
	struct SourceRanks
	{
		int filesystem = 0;
		int pmr = 0;
		int mdb = 0;
		int mxf = 0;
		int omf = 0;
		int avb = 0;

		int priority(MetadataSource source) const noexcept;
	};

	enum class SelectionRule
	{
		PreferredValue,
		MasterAssociations,
		FileDuration,
		DerivedEffect
	};

	struct PropertyPolicy
	{
		MediaProperty property;
		SelectionRule rule;
		SourceRanks sources;
	};

	using PropertyPolicies = std::array<PropertyPolicy, std::size_t(MediaProperty::Count)>;

	// Zero excludes a display candidate; eligible evidence still contributes to
	// read state and agreement. It does not establish format absence.
	// Unknown property values fail checked lookup instead of receiving a default rule.
	const PropertyPolicy &propertyPolicy(MediaProperty property);
	const PropertyPolicies &propertyPolicies() noexcept;

	// Source-ranked resolution retains alternatives. selectMetadata coordinates
	// master unions, compatible duration clocks and effect derivation separately.
	ResolvedField resolveProperty(const MediaEvidence &evidence, const PropertyPolicy &policy);
}
