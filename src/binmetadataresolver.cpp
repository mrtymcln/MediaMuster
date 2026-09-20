#include "binmetadataresolver.h"
#include "avbparser.h"
#include "mediafile.h"
#include "mobid.h"
#include "omfuid.h"

void BinMetadataResolver::Metadata::merge(const AvbMob &mob)
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
	m_metadata.clear();
	for (const AvbBin &bin : bins)
	{
		if (!bin.valid || !bin.complete)
			continue;
		for (const AvbMob &mob : bin.mobs)
		{
			// Source names describe imports/tapes; only master clips supply editor names.
			if (mob.mobType != AvbMob::masterMobType || mob.mobId.isEmpty())
				continue;
			m_metadata[mob.mobId].merge(mob);
			// OMF wrappers preserve their identity bytes in both the bin and
			// the database. Only MXF identities need the byte-order alias.
			if (!OmfUid::isOmfForm(mob.mobId))
			{
				const QString alias = MobId::toPmrForm(mob.mobId);
				if (!alias.isEmpty() && alias != mob.mobId)
					m_metadata[alias].merge(mob);
			}
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
	const auto found = m_metadata.constFind(file.masterMobId);
	if (found != m_metadata.cend())
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
