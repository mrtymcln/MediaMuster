#pragma once

#include <QString>

namespace Bella
{
	struct MdbFileRecord;
	struct MxfFileInformation;
	struct OmfFileInformation;
	struct OmfNativeAudioInformation;

	struct CompressionNames
	{
		QString compression;
		QString newDnx;
		QString oldDnx;
		QString reallyOldDnx;
	};

	struct EffectNames
	{
		QString effect;
		QString category;
	};

	// One already-selected eligible reader value; no file I/O or source choice.
	[[nodiscard]] CompressionNames compressionFor(const MdbFileRecord &);
	[[nodiscard]] CompressionNames compressionFor(const MxfFileInformation &);
	[[nodiscard]] CompressionNames compressionFor(const OmfFileInformation &);
	[[nodiscard]] CompressionNames compressionFor(const OmfNativeAudioInformation &);

	// Called when optional details are requested, using the final selected Type/name.
	// Ordinary Media/unknown Type gives blanks; unknown Precompute names keep the
	// existing lookup's token/category behavior. No Sequence or lookup status escapes.
	[[nodiscard]] EffectNames effectNamesFor(bool confirmedPrecompute, const QString &selectedClipName);
} // namespace Bella
