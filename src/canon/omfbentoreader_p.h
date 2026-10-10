#pragma once

// The private container reader behind Canon's OMF media reader. It follows Bento's
// table of contents and preserves each recorded value, without deciding what
// Avid's property names mean or which value a Media File row should display.

#include "scanmodel.h"
#include <QIODevice>

namespace Canon::Detail
{
	struct BentoValue
	{
		quint32 object = 0;
		quint32 property = 0;
		quint32 type = 0;
		quint32 generation = 0;
		quint32 referenceListObject = 0;
		QByteArray bytes;
		QVector<ByteRange> ranges;
		QVector<ByteRange> tocRanges;
		PropertyReadState state = PropertyReadState::NotRead;
		QString problem;
		bool bytesRetained = true; // Internal extents and optional media payloads stay as ranges.
	};

	struct BentoReadResult
	{
		ParsedSource::Outcome outcome = ParsedSource::Outcome::NotRead;
		quint16 major = 0;
		bool containerBigEndian = false;
		QVector<BentoValue> values; // Original order; only explicitly continued segments join.
		QVector<RawProperty> structure;
		QStringList diagnostics;
	};

	struct BentoReadOptions
	{
		bool metadataOnly = false; // Keep known essence as ranges; read descriptor summaries.
		qint64 labelOffset = -1;   // Absolute file offset; -1 means the final 24 bytes.
	};

	BentoReadResult readBento(QIODevice &source, const Cancellation &cancellation,
							  const BentoReadOptions &options = {});
}
