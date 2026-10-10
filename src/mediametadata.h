#pragma once

#include <QString>

// Small presentation helpers shared by the current format projectors.
// Format interpretation and value selection belong to the media engine.

/// Shared PCM display name. Classification uses descriptor/label evidence.
inline constexpr char kPcmAudioName[] = "PCM";

namespace MediaMetadataUtil
{
	/// A recorded import path may use either OS's separators, regardless of this host.
	[[nodiscard]] QString sourceFileBaseName(const QString &path);
}
