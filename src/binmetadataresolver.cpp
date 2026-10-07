#include "binmetadataresolver.h"
#include "avbparser.h"
#include "canon/projection.h"
#include "mediafile.h"

void BinMetadataResolver::MasterMobMetadata::merge(const AvbMob &mob, const QSharedPointer<const Canon::ParsedSource> &source)
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
	file.evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Avb);
	file.evidence.excludeSource(MediaProperty::OriginalBin, MetadataSource::Avb);
	if (file.clipNameSource == MediaFile::ClipNameSource::Avb)
	{
		file.clipName.clear();
		file.clipNameSource = MediaFile::ClipNameSource::None;
	}
	if (file.originalBinFromAvb)
	{
		file.originalBin.clear();
		file.originalBinFromAvb = false;
	}
	MasterMobMetadata combined;
	const auto identities = file.masterMobIds.isEmpty() ? QStringList{file.masterMobId} : file.masterMobIds;
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
			if (!file.canonAvbSources.contains(source))
				file.canonAvbSources.append(source);
	}
	for (const auto &value : combined.names)
		file.evidence.observe(MediaProperty::ClipName, value);
	for (const auto &value : combined.bins)
		file.evidence.observe(MediaProperty::OriginalBin, value);
	// Selection is per field. AVB remains a fallback; header/database facts keep
	// their agreed priority, and every alternative stays inspectable in RAM.
	const auto nameRank = [](MetadataSource source)
	{ return source == MetadataSource::Mxf || source == MetadataSource::Omf ? 3 : source == MetadataSource::Mdb ? 2
																			  : source == MetadataSource::Avb	? 1
																												: 0; };
	const auto binRank = [](MetadataSource source)
	{ return source == MetadataSource::Mdb ? 3 : source == MetadataSource::Mxf || source == MetadataSource::Omf ? 2
											 : source == MetadataSource::Avb									? 1
																												: 0; };
	auto selectedName = file.evidence.resolve(MediaProperty::ClipName, nameRank, QStringLiteral("Header > MDB > AVB"));
	auto selectedBin = file.evidence.resolve(MediaProperty::OriginalBin, binRank, QStringLiteral("MDB > header > AVB"));
	const auto qualifyConflict = [&](MediaProperty property, bool conflict, ResolvedField &selected)
	{
		if (conflict && selected.selectedObservation >= 0 &&
			file.evidence.observations(property)[selected.selectedObservation].snapshot->source == MetadataSource::Avb)
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
	combined.nameConflict |= selectedName.selectedObservation < 0 && selectedName.agreement == PropertyAgreement::Conflicting;
	combined.binConflict |= selectedBin.selectedObservation < 0 && selectedBin.agreement == PropertyAgreement::Conflicting;
	if (file.clipName.isEmpty() && selectedName.selectedObservation >= 0)
	{
		const auto source = file.evidence.observations(MediaProperty::ClipName)[selectedName.selectedObservation].snapshot->source;
		if (source != MetadataSource::Avb || !combined.nameConflict)
		{
			file.clipName = selectedName.value.toString();
			file.clipNameSource = source == MetadataSource::Avb	  ? MediaFile::ClipNameSource::Avb
								  : source == MetadataSource::Mdb ? MediaFile::ClipNameSource::Mdb
																  : MediaFile::ClipNameSource::MaterialPackage;
		}
	}
	if (file.originalBin.isEmpty() && selectedBin.selectedObservation >= 0)
	{
		const auto source = file.evidence.observations(MediaProperty::OriginalBin)[selectedBin.selectedObservation].snapshot->source;
		if (source != MetadataSource::Avb || !combined.binConflict)
		{
			file.originalBin = selectedBin.value.toString();
			file.originalBinFromAvb = source == MetadataSource::Avb;
		}
	}
	// Pre-Canon callers may still provide only the compatibility values.
	if (file.clipName.isEmpty() && !combined.nameConflict && combined.names.isEmpty() && !combined.clipName.isEmpty())
	{
		file.clipName = combined.clipName;
		file.clipNameSource = MediaFile::ClipNameSource::Avb;
	}
	if (file.originalBin.isEmpty() && !combined.binConflict && combined.bins.isEmpty() && !combined.originalBin.isEmpty())
	{
		file.originalBin = combined.originalBin;
		file.originalBinFromAvb = true;
	}
	if (file.canonScan)
	{
		Canon::selectEffectMetadata(file.evidence);
		file.effect = file.evidence.selected(MediaProperty::Effect).value.toString();
		file.effectCategory = file.evidence.selected(MediaProperty::EffectCategory).value.toString();
		file.effectSequence = file.evidence.selected(MediaProperty::EffectSequence).value.toString();
	}
	return file.clipName != previousName || file.originalBin != previousBin;
}
