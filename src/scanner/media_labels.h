#pragma once

#include <QString>

namespace Bella
{
	struct MdbFileRecord;
	struct MxfFileInformation;
	struct OmfFileInformation;
	struct OmfNativeAudioInformation;

	// MARK: - Display names

	struct CompressionNames
	{
		QString compressionName;
		QString newDnx;
		QString oldDnx;
		QString reallyOldDnx;
	};

	struct EffectNames
	{
		QString effectDisplayText;
		QString effectCategoryText;
	};

	// MARK: - Compression and effect naming

	// One already-selected eligible reader value; no file I/O or source choice.
	[[nodiscard]] CompressionNames compressionNamesFor(const MdbFileRecord &);
	[[nodiscard]] CompressionNames compressionNamesFor(const MxfFileInformation &);
	[[nodiscard]] CompressionNames compressionNamesFor(const OmfFileInformation &);
	[[nodiscard]] CompressionNames compressionNamesFor(const OmfNativeAudioInformation &);

	// Called when optional details are requested, using the final selected Type/name.
	// Ordinary Media/unknown Type gives blanks; unknown Precompute names keep the
	// existing lookup's token/category behavior. No Sequence or lookup status escapes.
	[[nodiscard]] EffectNames effectNamesFor(bool confirmedPrecompute, const QString &selectedClipName);
} // namespace Bella
