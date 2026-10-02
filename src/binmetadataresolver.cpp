#include "binmetadataresolver.h"
#include "avbparser.h"
#include "mediafile.h"

void BinMetadataResolver::MasterMobMetadata::merge(const AvbMob &mob)
{
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
			m_metadataByMasterMobId[mob.mobId].merge(mob);
		}
	}
}

bool BinMetadataResolver::apply(MediaFile &file) const
{
	const QString previousName = file.clipName;
	const QString previousBin = file.originalBin;
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
	const auto found = m_metadataByMasterMobId.constFind(file.masterMobId);
	if (found != m_metadataByMasterMobId.cend())
	{
		const auto &value = found.value();
		if (file.clipName.isEmpty() && !value.nameConflict && !value.clipName.isEmpty())
		{
			file.clipName = value.clipName;
			file.clipNameSource = MediaFile::ClipNameSource::Avb;
		}
		if (file.originalBin.isEmpty() && !value.binConflict && !value.originalBin.isEmpty())
		{
			file.originalBin = value.originalBin;
			file.originalBinFromAvb = true;
		}
	}
	return file.clipName != previousName || file.originalBin != previousBin;
}
