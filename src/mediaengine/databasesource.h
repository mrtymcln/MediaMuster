#pragma once

// Projects a database through its reader using a temporary RAM buffer. Returned
// facts and their evidence own everything they need after that buffer is gone.

#include "sourcepreparation.h"
#include <stdexcept>

namespace MediaEngine
{
	class DatabaseSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	PreparedSource prepareDatabase(const SourceCandidate &candidate, const QString &readReason,
								   const Cancellation &cancellation);
}
