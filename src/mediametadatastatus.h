#pragma once

#include "mediaevidence.h"
#include <array>
#include <cstddef>
#include <optional>

/// Inline source-kind membership; contains no source paths or owners.
class MetadataSourceSet
{
public:
	void add(MetadataSource source);
	bool contains(MetadataSource source) const;
	quint8 bits() const noexcept { return m_bits; }

private:
	quint8 m_bits = 0;
};

/// Selected-field conclusions without values, observations or interpretation history.
struct MediaFieldStatus
{
	PropertyReadState readState = PropertyReadState::NotRead;
	PropertyReadReason readReason = PropertyReadReason::NoAssociatedSource;
	PropertyAgreement agreement = PropertyAgreement::NotCompared;
	PropertyApplicability applicability = PropertyApplicability::Unknown;
	bool hasSelectedValue = false;
	bool captured = false;
	// Principal selected observation only. Master-association unions have no
	// principal origin; a duration's supplementary display clock may use other inputs.
	std::optional<MetadataSource> selectedSource;
	std::optional<EvidenceBasis> selectedBasis;
	std::optional<SourceFreshness> selectedFreshness;
	/// Eligible sources with Present observations, not necessarily selected contributors.
	MetadataSourceSet presentSources;
	MetadataSourceSet unknownFreshnessSources;
	MetadataSourceSet timestampConsistentSources;
	/// Includes changes that excluded a source from the selected metadata.
	MetadataSourceSet changedSources;
};

enum class MediaHeaderReadOutcome : quint8
{
	NoReceipt,
	NotRead,
	Complete,
	Incomplete,
	Malformed,
	Unsupported,
	IoError,
	Cancelled
};

/// Fixed checklist for the current property catalogue; owns no scan/session data.
class MediaMetadataStatus
{
public:
	const MediaFieldStatus &field(MediaProperty property) const;
	MediaFieldStatus &field(MediaProperty property);
	MediaHeaderReadOutcome headerOutcome = MediaHeaderReadOutcome::NoReceipt;

private:
	std::array<MediaFieldStatus, static_cast<std::size_t>(MediaProperty::Count)> m_fields{};
};

MediaMetadataStatus makeMediaMetadataStatus(const MediaEvidence &evidence,
	MediaHeaderReadOutcome headerOutcome = MediaHeaderReadOutcome::NoReceipt);
