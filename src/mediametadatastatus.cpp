#include "mediametadatastatus.h"
#include <stdexcept>

namespace
{
	quint8 sourceBit(MetadataSource source)
	{
		switch (source)
		{
		case MetadataSource::Filesystem:
		case MetadataSource::Pmr:
		case MetadataSource::Mdb:
		case MetadataSource::Mxf:
		case MetadataSource::Omf:
		case MetadataSource::Avb:
			return static_cast<quint8>(quint8{1} << static_cast<unsigned int>(source));
		}
		throw std::invalid_argument("Unknown metadata source.");
	}

	void addFreshness(MediaFieldStatus &status, MetadataSource source, SourceFreshness freshness, bool eligible)
	{
		if (freshness == SourceFreshness::Changed)
			status.changedSources.add(source);
		else if (eligible && freshness == SourceFreshness::TimestampConsistent)
			status.timestampConsistentSources.add(source);
		else if (eligible && freshness == SourceFreshness::Unknown)
			status.unknownFreshnessSources.add(source);
	}
}

void MetadataSourceSet::add(MetadataSource source)
{
	m_bits = static_cast<quint8>(m_bits | sourceBit(source));
}

bool MetadataSourceSet::contains(MetadataSource source) const
{
	return (m_bits & sourceBit(source)) != 0;
}

const MediaFieldStatus &MediaMetadataStatus::field(MediaProperty property) const
{
	return m_fields.at(static_cast<std::size_t>(property));
}

MediaFieldStatus &MediaMetadataStatus::field(MediaProperty property)
{
	return m_fields.at(static_cast<std::size_t>(property));
}

MediaMetadataStatus makeMediaMetadataStatus(const MediaEvidence &evidence, MediaHeaderReadOutcome headerOutcome)
{
	MediaMetadataStatus result;
	result.headerOutcome = headerOutcome;
	for (int index = 0; index < static_cast<int>(MediaProperty::Count); ++index)
	{
		const auto property = static_cast<MediaProperty>(index);
		const auto selected = evidence.selected(property);
		auto &status = result.field(property);
		status.readState = selected.readState;
		status.readReason = selected.readReason;
		status.agreement = selected.agreement;
		status.applicability = selected.applicability;
		status.hasSelectedValue = selected.value.isValid();
		status.captured = true;
		const auto &observations = evidence.observations(property);
		if (property != MediaProperty::MasterMobId && status.hasSelectedValue &&
			selected.selectedObservation >= 0 && selected.selectedObservation < observations.size())
		{
			const auto &principal = observations[selected.selectedObservation];
			if (principal.snapshot && principal.eligible && principal.readState == PropertyReadState::Present)
			{
				status.selectedSource = principal.snapshot->source;
				status.selectedBasis = principal.basis;
				status.selectedFreshness = principal.freshness;
			}
		}
		for (const auto &coverage : evidence.sourceCoverage())
			if (coverage.snapshot)
				addFreshness(status, coverage.snapshot->source, coverage.freshness, coverage.eligible);
		for (const auto &observation : observations)
		{
			if (!observation.snapshot)
				continue;
			const auto source = observation.snapshot->source;
			if (observation.eligible && observation.readState == PropertyReadState::Present)
				status.presentSources.add(source);
			addFreshness(status, source, observation.freshness, observation.eligible);
		}
	}
	return result;
}
