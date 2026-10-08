#include "metadataselectionpolicy.h"

#include <initializer_list>
#include <stdexcept>

namespace Canon
{
	namespace
	{
		using Source = MetadataSource;

		constexpr int &sourceRank(SourceRanks &ranks, Source source)
		{
			switch (source)
			{
			case Source::Filesystem:
				return ranks.filesystem;
			case Source::Pmr:
				return ranks.pmr;
			case Source::Mdb:
				return ranks.mdb;
			case Source::Mxf:
				return ranks.mxf;
			case Source::Omf:
				return ranks.omf;
			case Source::Avb:
				return ranks.avb;
			}
			throw std::invalid_argument("Unknown source in metadata preference group.");
		}

		// Compile named groups into the resolver's existing ranks. Invalid or repeated
		// sources make a constexpr policy fail compilation rather than silently overwrite it.
		constexpr SourceRanks prefer(std::initializer_list<Source> first,
			std::initializer_list<Source> second = {}, std::initializer_list<Source> third = {})
		{
			SourceRanks ranks;
			int priority = 3;
			for (const auto group : {first, second, third})
			{
				for (const auto source : group)
				{
					auto &rank = sourceRank(ranks, source);
					if (rank != 0)
						throw std::invalid_argument("Repeated source in metadata preference groups.");
					rank = priority;
				}
				--priority;
			}
			return ranks;
		}

		// Read each prefer() left to right: first choice, fallback, final fallback.
		// Sources in one group are equal. Conflicting top-group values remain unresolved.
		// Unlisted sources cannot supply the selected value; their evidence is retained.
		// Source::Omf includes the LegacyReader's OMF, WAV and AIFF observations.
		// These are application field names; observations retain original source property names.
		// Edit one row for the next build. Keep one row per property in catalogue order.
		constexpr PropertyPolicies kPropertyPolicies
		{{
			// Editor's clip name: header first, MDB second, loaded AVB third.
			{MediaProperty::ClipName, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb}, {Source::Avb})},

			// Project association: PMR first, MDB second, media header third.
			{MediaProperty::Project, SelectionRule::PreferredValue,
			 prefer({Source::Pmr}, {Source::Mdb}, {Source::Mxf, Source::Omf})},

			// Original bin association: MDB first, a supplied header value second, AVB third.
			{MediaProperty::OriginalBin, SelectionRule::PreferredValue,
			 prefer({Source::Mdb}, {Source::Mxf, Source::Omf}, {Source::Avb})},

			// Audio or video, interpreted from the owning descriptor or recorded labels.
			{MediaProperty::Kind, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// This file's length and unit rate; supplement only with a compatible display clock.
			{MediaProperty::FileDuration, SelectionRule::FileDuration,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Master-clip track lengths retain their individual master and track context.
			{MediaProperty::ClipDuration, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Physical file size from the filesystem, independently of any Avid descriptor.
			{MediaProperty::Size, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// Readable compression name for the Compression column, interpreted from format evidence.
			{MediaProperty::Compression, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Current DNx naming scheme, for example Avid DNx HQX.
			{MediaProperty::NewDnx, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Previous DNx naming scheme, for example DNxHD HQX or DNxHR HQX.
			{MediaProperty::OldDnx, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Historical bitrate name, for example DNxHD 175x, when the recorded format supports it.
			{MediaProperty::ReallyOldDnx, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Visible image dimensions interpreted from the stored, sampled and display geometry.
			{MediaProperty::Resolution, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Video rate retained as an exact fraction, separately from the audio sampling rate.
			{MediaProperty::FrameRate, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Audio sampling rate retained as an exact fraction when representable.
			{MediaProperty::SampleRate, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Interpreted sample precision for the Bit Depth column, not a float/integer decision.
			{MediaProperty::BitDepth, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Internal numeric representation: integer, float or fixed point, with supporting
			// evidence.
			{MediaProperty::SampleFormat, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Whether format evidence establishes an alpha component; unknown remains unresolved.
			{MediaProperty::Alpha, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Media or precompute classification, established independently of an editable clip name.
			{MediaProperty::Type, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Interpreted precompute category, retaining its recorded usage evidence.
			{MediaProperty::PrecomputeCategory, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Effect catalogue category inferred from the selected precompute name; not proof of
			// effect identity.
			{MediaProperty::EffectCategory, SelectionRule::DerivedEffect,
			 prefer({Source::Mdb, Source::Mxf, Source::Omf, Source::Avb})},

			// Readable effect description inferred from the selected precompute name.
			{MediaProperty::Effect, SelectionRule::DerivedEffect,
			 prefer({Source::Mdb, Source::Mxf, Source::Omf, Source::Avb})},

			// Sequence name inferred from the selected precompute name when the naming pattern
			// supplies it.
			{MediaProperty::EffectSequence, SelectionRule::DerivedEffect,
			 prefer({Source::Mdb, Source::Mxf, Source::Omf, Source::Avb})},

			// Filesystem birth time for Date Created; not an Avid clip or mob creation date.
			{MediaProperty::Created, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// The filename at this physical location; a database claim cannot replace it.
			{MediaProperty::Filename, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// Original source basename interpreted from a recorded import path or locator.
			{MediaProperty::SourceFilename, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// The original import/source path recorded by Avid, not this file's current path.
			{MediaProperty::SourcePath, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Interpreted import-settings container tag; separate from the MXF wrapping label below.
			{MediaProperty::SourceContainer, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Import classification based on qualified import-settings evidence.
			{MediaProperty::Imported, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Current physical path from the filesystem; moves update the row's location.
			{MediaProperty::Location, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// Filesystem modification timestamp, separately from Avid object timestamps.
			{MediaProperty::Modified, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// The filesystem volume identifier used by the physical file's scan receipt.
			{MediaProperty::VolumeIdentifier, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// This file's Avid identity: header first, matching MDB second, PMR claim third.
			{MediaProperty::FileMobId, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb}, {Source::Pmr})},

			// Keep every eligible master association. This union rule does not use source preferences.
			{MediaProperty::MasterMobId, SelectionRule::MasterAssociations,
			 {}},

			// Local database membership/readability, derived from this folder's scan outcomes.
			{MediaProperty::DatabaseStatus, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// Whether this row belongs to the admitted legacy folder family; not proof of an OMF
			// container.
			{MediaProperty::OmfScan, SelectionRule::PreferredValue,
			 prefer({Source::Filesystem})},

			// This file's recorded audio channel count; sibling audio files do not add channels here.
			{MediaProperty::Channels, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Internal binary coding label, read or normalized from format evidence; supports
			// Compression interpretation.
			{MediaProperty::CompressionLabel, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Internal essence-container/wrapping label; MXF supplies FileDescriptor.EssenceContainer.
			{MediaProperty::WrappingLabel, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Recorded pixel-component layout, retained separately from its interpreted depth and
			// sample format.
			{MediaProperty::PixelLayout, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Recorded drop-frame numbering flag; it does not change the stored duration units.
			{MediaProperty::DropFrame, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},

			// Recorded component-depth encoding, which can differ from displayed precision for DNx
			// sentinel values.
			{MediaProperty::ComponentDepth, SelectionRule::PreferredValue,
			 prefer({Source::Mxf, Source::Omf}, {Source::Mdb})},
		}};

		constexpr bool validPolicies()
		{
			for (std::size_t index = 0; index < kPropertyPolicies.size(); ++index)
			{
				const auto &policy = kPropertyPolicies[index];
				if (std::size_t(policy.property) != index)
					return false;
				for (const auto rank : {policy.sources.filesystem, policy.sources.pmr,
										policy.sources.mdb, policy.sources.mxf, policy.sources.omf, policy.sources.avb})
					if (rank < 0 || rank > 3)
						return false;
			}
			return true;
		}

		static_assert(validPolicies(),
					  "Every MediaProperty needs one ordered, valid preference policy.");
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
		return evidence.resolve(policy.property, [&policy](MetadataSource source)
		{
			return policy.sources.priority(source);
		}, mediaPropertyName(policy.property));
	}
}
