#pragma once

// The private container reader behind Canon's MDB reader. It follows Bento's
// table of contents and preserves each recorded value, without deciding what
// Avid's property names mean or which value a media-file row should display.

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
		bool bytesRetained = true; // Internal TOC/container extents are kept as ranges.
	};

	struct BentoReadResult
	{
		ParsedSource::Outcome outcome = ParsedSource::Outcome::NotRead;
		quint16 major = 0;
		bool containerBigEndian = false;
		bool metadataBigEndian = false; // Initial format default; MDB declarations may override it.
		QVector<BentoValue> values; // Original order; only explicitly continued segments join.
		QVector<RawProperty> structure;
		QStringList diagnostics;
	};

	BentoReadResult readBento(QIODevice &source, const Cancellation &cancellation);
}
