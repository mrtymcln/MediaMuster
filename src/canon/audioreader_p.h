#pragma once

// Reads the native metadata around legacy WAV and AIF audio. Sound samples
// stay on disk; embedded OMF chunks are returned for the OMF reader to inspect.

#include "sourcereader.h"

namespace Canon::Detail
{
    struct AudioReadResult
    {
        ParsedSource source;
        QVector<ByteRange> omfiChunks;
        ByteRange trailingBytes; // Physical bytes beyond the declared RIFF/FORM extent.
    };

    AudioReadResult readChunkedAudio(QIODevice &device, const ReaderContext &context);
}
