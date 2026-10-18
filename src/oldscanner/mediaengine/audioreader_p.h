#pragma once

// Reads the native metadata around legacy WAV and AIF audio. Sound samples
// stay on disk; embedded OMF chunks are returned for the OMF reader to inspect.

#include "mediaengine/sourcereader.h"
#include <QVariantMap>

namespace MediaEngine::Detail
{
    struct AudioReadResult
    {
        ParsedSource source;
        QVector<ByteRange> omfiChunks;
        ByteRange trailingBytes; // Physical bytes beyond the declared RIFF/FORM extent.
    };

    AudioReadResult readChunkedAudio(QIODevice &device, const ReaderContext &context);

    // Native chunks and OMF summaries share these fields. Preserve each field's
    // bytes/location/read state; a missing extension cannot erase readable base fields.
    struct AudioFormatResult
    {
        QVariantMap values;
        QVector<RawProperty> fields;
        QStringList diagnostics;
    };

    AudioFormatResult decodeWaveFormat(const RawProperty &property);
    AudioFormatResult decodeAiffCommon(const RawProperty &property, bool compressed);
    QVector<RawProperty> decodeAudioSummary(const RawProperty &summary);
}
