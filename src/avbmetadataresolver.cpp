#include "avbmetadataresolver.h"
#include "avbbinloader.h"
#include "mediaengineadapter.h"
#include "mediaengine/metadataselectionpolicy.h"
#include "mediaengine/projection.h"
#include "mediafile.h"

void AvbMetadataResolver::MasterMobMetadata::merge(const AvbComposition &mob, const QSharedPointer<const MediaEngine::ParsedSource> &source)
{
	clipNameObservations += mob.nameObservations;
	originalBinObservations += mob.originalBinObservations;
	if (source && !avbSources.contains(source))
		avbSources.append(source);
	if (!mob.name.isEmpty())
	{
		if (!clipName.isEmpty() && clipName != mob.name)
			clipNameConflict = true;
		else
			clipName = mob.name;
	}
	if (!mob.originalBinUid.isEmpty())
	{
		if (!originalBinUid.isEmpty() && originalBinUid != mob.originalBinUid)
			originalBinConflict = true;
		else
			originalBinUid = mob.originalBinUid;
	}
	if (!mob.originalBinName.isEmpty())
	{
		if (!originalBinName.isEmpty() && originalBinName != mob.originalBinName)
			originalBinConflict = true;
		else
			originalBinName = mob.originalBinName;
	}
}

void AvbMetadataResolver::setBins(const QVector<AvbBin> &bins)
{
	m_metadataByMasterMobId.clear();
	for (const AvbBin &bin : bins)
	{
		if (!bin.isUsable())
			continue;
		for (const AvbComposition &mob : bin.compositions)
		{
			// Source names describe imports/tapes; only master clips supply editor names.
			if (mob.mobType != AvbComposition::masterMobType || mob.mobId.isEmpty())
				continue;
			// Scanner rows and AVB compositions already share the database ID representation.
			m_metadataByMasterMobId[mob.mobId].merge(mob, bin.sourceGraph);
		}
	}
}

bool AvbMetadataResolver::applyTo(MediaFile &file) const
{
	const QString previousName = file.clipName;
	const QString previousBin = file.originalBin;
	const auto previousNameSource = file.clipNameSource;
	const bool previousBinFromAvb = file.originalBinFromAvb;
	file.evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Avb);
	file.evidence.excludeSource(MediaProperty::OriginalBin, MetadataSource::Avb);
	MasterMobMetadata combined;
	const auto identities = file.evidence.selected(MediaProperty::MasterMobId).value.toStringList();
	for (const auto &identity : identities)
	{
		const auto found = m_metadataByMasterMobId.constFind(identity);
		if (found == m_metadataByMasterMobId.cend())
			continue;
		const auto &value = found.value();
		AvbComposition mob;
		mob.name = value.clipName;
		mob.originalBinName = value.originalBinName;
		mob.originalBinUid = value.originalBinUid;
		mob.nameObservations = value.clipNameObservations;
		mob.originalBinObservations = value.originalBinObservations;
		combined.merge(mob, {});
		combined.clipNameConflict |= value.clipNameConflict;
		combined.originalBinConflict |= value.originalBinConflict;
		for (const auto &source : value.avbSources)
			if (!file.mediaEngineAvbSources.contains(source))
			{
				file.mediaEngineAvbSources.append(source);
				if (source)
					file.evidence.registerSource(source->snapshot);
			}
	}
	for (const auto &value : combined.clipNameObservations)
		file.evidence.observe(MediaProperty::ClipName, value);
	for (const auto &value : combined.originalBinObservations)
		file.evidence.observe(MediaProperty::OriginalBin, value);
	auto selectedName = MediaEngine::resolveProperty(file.evidence, MediaEngine::propertyPolicy(MediaProperty::ClipName));
	auto selectedBin = MediaEngine::resolveProperty(file.evidence, MediaEngine::propertyPolicy(MediaProperty::OriginalBin));
	const auto qualifyConflict = [&](MediaProperty property, bool conflict, ResolvedField &selected)
	{
		const auto &observations = file.evidence.observations(property);
		if (conflict && selected.selectedObservation >= 0 && selected.selectedObservation < observations.size() &&
			observations[selected.selectedObservation].snapshot && observations[selected.selectedObservation].snapshot->source == MetadataSource::Avb)
		{
			selected.value.clear();
			selected.selectedObservation = -1;
			selected.agreement = PropertyAgreement::Conflicting;
			selected.reason = EvidenceExplanation(EvidenceExplanation::Reason::AvbAssociationConflict);
		}
	};
	qualifyConflict(MediaProperty::ClipName, combined.clipNameConflict, selectedName);
	qualifyConflict(MediaProperty::OriginalBin, combined.originalBinConflict, selectedBin);
	file.evidence.select(MediaProperty::ClipName, selectedName);
	file.evidence.select(MediaProperty::OriginalBin, selectedBin);
	MediaEngine::selectEffectMetadata(file.evidence);
	const bool changed = applyResolvedMetadata(file);
	return changed || file.clipName != previousName || file.originalBin != previousBin ||
		   file.clipNameSource != previousNameSource || file.originalBinFromAvb != previousBinFromAvb;
}
