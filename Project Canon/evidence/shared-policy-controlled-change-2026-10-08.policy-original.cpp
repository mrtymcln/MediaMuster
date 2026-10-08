#include "metadataselectionpolicy.h"

#include <stdexcept>

namespace Canon
{
	namespace
	{
		// Edit these preferences for the next build. Ranks are 3 > 2 > 1 > 0;
		// equal ranks form one tier. Keep one row per property in catalogue order.
		// Increase a row's version when changing its selection policy.
		constexpr PropertyPolicies kPropertyPolicies{{
			//                                      FS PMR MDB MXF OMF AVB
			{MediaProperty::ClipName, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 1}, "clip-name", 1},
			{MediaProperty::Project, SelectionRule::PreferredValue,
				{0, 3, 2, 1, 1, 0}, "project", 1},
			{MediaProperty::OriginalBin, SelectionRule::PreferredValue,
				{0, 0, 3, 2, 2, 1}, "original-bin", 1},
			{MediaProperty::Kind, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "media-kind", 1},
			{MediaProperty::FileDuration, SelectionRule::FileDuration,
				{0, 0, 2, 3, 3, 0}, "file-duration", 1},
			{MediaProperty::ClipDuration, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "clip-track-durations", 1},
			{MediaProperty::Size, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-size", 1},
			{MediaProperty::Codec, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "codec", 1},
			{MediaProperty::NewDnx, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "dnx-current-name", 1},
			{MediaProperty::OldDnx, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "dnx-previous-name", 1},
			{MediaProperty::ReallyOldDnx, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "dnx-legacy-name", 1},
			{MediaProperty::Resolution, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "visible-resolution", 1},
			{MediaProperty::FrameRate, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "video-rate", 1},
			{MediaProperty::SampleRate, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "audio-rate", 1},
			{MediaProperty::BitDepth, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "bit-depth", 1},
			{MediaProperty::SampleFormat, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "sample-representation", 1},
			{MediaProperty::Alpha, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "alpha", 1},
			{MediaProperty::Type, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "media-role", 1},
			{MediaProperty::PrecomputeCategory, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "precompute-category", 1},
			{MediaProperty::EffectCategory, SelectionRule::DerivedEffect,
				{0, 0, 3, 3, 3, 3}, "effect-category-from-selected-name", 1},
			{MediaProperty::Effect, SelectionRule::DerivedEffect,
				{0, 0, 3, 3, 3, 3}, "effect-from-selected-name", 1},
			{MediaProperty::EffectSequence, SelectionRule::DerivedEffect,
				{0, 0, 3, 3, 3, 3}, "effect-sequence-from-selected-name", 1},
			{MediaProperty::Created, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-created", 1},
			{MediaProperty::Filename, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-filename", 1},
			{MediaProperty::SourceFilename, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "source-filename", 1},
			{MediaProperty::SourcePath, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "source-path", 1},
			{MediaProperty::SourceContainer, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "source-container", 1},
			{MediaProperty::Imported, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "imported", 1},
			{MediaProperty::Location, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-location", 1},
			{MediaProperty::Modified, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-modified", 1},
			{MediaProperty::VolumeIdentifier, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "physical-volume", 1},
			{MediaProperty::FileMobId, SelectionRule::PreferredValue,
				{0, 1, 2, 3, 3, 0}, "file-identity", 1},
			{MediaProperty::MasterMobId, SelectionRule::MasterAssociations,
				{}, "master-associations", 1},
			{MediaProperty::DatabaseStatus, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "local-database-membership", 1},
			{MediaProperty::OmfScan, SelectionRule::PreferredValue,
				{3, 0, 0, 0, 0, 0}, "managed-media-family", 1},
			{MediaProperty::Channels, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "audio-channels", 1},
			{MediaProperty::CompressionLabel, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "compression-label", 1},
			{MediaProperty::WrappingLabel, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "wrapping-label", 1},
			{MediaProperty::PixelLayout, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "pixel-layout", 1},
			{MediaProperty::DropFrame, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "drop-frame", 1},
			{MediaProperty::ComponentDepth, SelectionRule::PreferredValue,
				{0, 0, 2, 3, 3, 0}, "component-depth", 1}
		}};

		constexpr bool validPolicies()
		{
			for (std::size_t index = 0; index < kPropertyPolicies.size(); ++index)
			{
				const auto &policy = kPropertyPolicies[index];
				if (std::size_t(policy.property) != index || !policy.ruleId || !*policy.ruleId ||
					policy.version == 0)
					return false;
				for (const auto rank : {policy.sources.filesystem, policy.sources.pmr,
					policy.sources.mdb, policy.sources.mxf, policy.sources.omf, policy.sources.avb})
					if (rank < 0 || rank > 3)
						return false;
			}
			return true;
		}

		static_assert(validPolicies(),
			"Every MediaProperty needs one ordered policy row with ranks 0..3 and a rule/version.");
	}

	int SourceRanks::priority(MetadataSource source) const noexcept
	{
		switch (source)
		{
		case MetadataSource::Filesystem:
			return filesystem;
		case MetadataSource::Pmr:
			return pmr;
		case MetadataSource::Mdb:
			return mdb;
		case MetadataSource::Mxf:
			return mxf;
		case MetadataSource::Omf:
			return omf;
		case MetadataSource::Avb:
			return avb;
		}
		return 0;
	}

	const PropertyPolicy &propertyPolicy(MediaProperty property)
	{
		const int index = int(property);
		if (index < 0 || std::size_t(index) >= kPropertyPolicies.size())
			throw std::out_of_range("Unknown MediaProperty in metadata selection policy.");
		return kPropertyPolicies[std::size_t(index)];
	}

	const PropertyPolicies &propertyPolicies() noexcept
	{
		return kPropertyPolicies;
	}

	ResolvedField resolveProperty(const MediaEvidence &evidence, const PropertyPolicy &policy)
	{
		auto result = evidence.resolve(policy.property, [&policy](MetadataSource source)
		{
			return policy.sources.priority(source);
		}, QString::fromLatin1(policy.ruleId));
		result.ruleVersion = policy.version;
		return result;
	}
}
