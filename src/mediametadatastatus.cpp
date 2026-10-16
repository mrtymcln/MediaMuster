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
		if (status.hasSelectedValue &&
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
	auto &resolution = result.field(MediaProperty::Resolution);
	if (!resolution.hasSelectedValue && resolution.readState != PropertyReadState::Present &&
		resolution.readState != PropertyReadState::Unreadable)
	{
		const auto &kind = result.field(MediaProperty::Kind);
		const auto selectedKind = evidence.selected(MediaProperty::Kind);
		constexpr int audioKind = 1; // The shared descriptor interpretation uses 0 video / 1 audio.
		const bool confirmedAudio = kind.hasSelectedValue && kind.readState == PropertyReadState::Present &&
									kind.selectedSource.has_value() && kind.selectedFreshness != SourceFreshness::Changed &&
									selectedKind.value.toInt() == audioKind;
		// Applicability follows the qualified file kind; unrelated unchecked
		// source contexts must not hide it or turn disagreement into certainty.
		if (kind.agreement == PropertyAgreement::Conflicting)
			resolution.applicability = PropertyApplicability::Unknown;
		else if (confirmedAudio)
			resolution.applicability = PropertyApplicability::NotApplicable;
		else if (resolution.applicability == PropertyApplicability::NotApplicable)
			resolution.applicability = PropertyApplicability::Unknown;
	}
	return result;
}
