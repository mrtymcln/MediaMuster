#pragma once

// Reads MXF metadata and returns its supported observations and source receipt.
// The reader's temporary records are released once projection has finished.

#include "sourcepreparation.h"
#include <QIODevice>
#include <stdexcept>

namespace MediaEngine
{
	class MxfSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	PreparedSource prepareMxf(const SourceCandidate &candidate, const QString &readReason,
							  const Cancellation &cancellation);
	// Borrows an already-open input and leaves it open. Controlled I/O tests use
	// this same reading path without inventing MXF structures.
	PreparedSource prepareMxf(QIODevice &input, const SourceCandidate &candidate,
							  const QString &readReason, const Cancellation &cancellation);
}
