#pragma once

// MDB's private container reader follows its Bento contents list and retains
// recorded values. It has its own implementation so removing OMF media support
// cannot change database reading. Property meanings belong to mdbobjects_p.

#include "scanmodel.h"
#include <QIODevice>

namespace MediaEngine::MdbDetail
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
		bool bytesRetained = true; // Internal container extents stay as ranges.
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

	BentoReadResult readBento(QIODevice &source, const Cancellation &cancellation);
}
