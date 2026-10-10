#include "binmetadataresolver.h"
#include "avbparser.h"
#include "mediaengineadapter.h"
#include "mediaengine/metadataselectionpolicy.h"
#include "mediaengine/projection.h"
#include "mediafile.h"

void BinMetadataResolver::MasterMobMetadata::merge(const AvbMob &mob, const QSharedPointer<const MediaEngine::ParsedSource> &source)
{
	names += mob.nameObservations;
	bins += mob.originalBinObservations;
	if (source && !sources.contains(source))
		sources.append(source);
	if (!mob.name.isEmpty())
	{
		if (!clipName.isEmpty() && clipName != mob.name)
			nameConflict = true;
		else
			clipName = mob.name;
	}
	if (!mob.originalBinUid.isEmpty())
	{
		if (!originalBinUid.isEmpty() && originalBinUid != mob.originalBinUid)
			binConflict = true;
		else
			originalBinUid = mob.originalBinUid;
	}
	if (!mob.originalBin.isEmpty())
	{
		if (!originalBin.isEmpty() && originalBin != mob.originalBin)
			binConflict = true;
		else
			originalBin = mob.originalBin;
	}
}

void BinMetadataResolver::setBins(const QVector<AvbBin> &bins)
{
	m_metadataByMasterMobId.clear();
	for (const AvbBin &bin : bins)
	{
		if (!bin.isUsable())
			continue;
		for (const AvbMob &mob : bin.mobs)
		{
			// Source names describe imports/tapes; only master clips supply editor names.
			if (mob.mobType != AvbMob::masterMobType || mob.mobId.isEmpty())
				continue;
			// Scanner rows and AVB compositions already share the database ID representation.
			m_metadataByMasterMobId[mob.mobId].merge(mob, bin.source);
		}
	}
}

bool BinMetadataResolver::apply(MediaFile &file) const
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
		AvbMob mob;
		mob.name = value.clipName;
		mob.originalBin = value.originalBin;
		mob.originalBinUid = value.originalBinUid;
		mob.nameObservations = value.names;
		mob.originalBinObservations = value.bins;
		combined.merge(mob, {});
		combined.nameConflict |= value.nameConflict;
		combined.binConflict |= value.binConflict;
		for (const auto &source : value.sources)
			if (!file.mediaEngineAvbSources.contains(source))
			{
				file.mediaEngineAvbSources.append(source);
				if (source)
					file.evidence.registerSource(source->snapshot);
			}
	}
	for (const auto &value : combined.names)
		file.evidence.observe(MediaProperty::ClipName, value);
	for (const auto &value : combined.bins)
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
			selected.reason = QStringLiteral("Matching AVB master or original-bin associations disagree; no value selected.");
		}
	};
	qualifyConflict(MediaProperty::ClipName, combined.nameConflict, selectedName);
	qualifyConflict(MediaProperty::OriginalBin, combined.binConflict, selectedBin);
	file.evidence.select(MediaProperty::ClipName, selectedName);
	file.evidence.select(MediaProperty::OriginalBin, selectedBin);
	MediaEngine::selectEffectMetadata(file.evidence);
	const bool changed = applyResolvedMetadata(file);
	return changed || file.clipName != previousName || file.originalBin != previousBin ||
		   file.clipNameSource != previousNameSource || file.originalBinFromAvb != previousBinFromAvb;
}
