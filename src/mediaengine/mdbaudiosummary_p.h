#pragma once

// Decodes audio format fields copied inside MDB descriptor summaries. This
// reads already-retained database bytes and has no native audio-file reader.

#include "scanmodel.h"

namespace MediaEngine::MdbDetail
{
	QVector<RawProperty> decodeAudioSummary(const RawProperty &summary);
}
