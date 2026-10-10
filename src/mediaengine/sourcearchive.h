#pragma once

// Keeps a source's complete metadata in compressed RAM after projection.
// Reading it back creates one temporary graph and reuses its original receipts.
// This is internal scan storage; it never creates a file or a database.

#include "scanmodel.h"
#include <stdexcept>

namespace MediaEngine
{
	class SourceArchiveError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class SourceArchive
	{
	public:
		static QSharedPointer<const SourceArchive> pack(const ParsedSource &source,
														const Cancellation &cancellation);
		std::optional<ParsedSource> restore(const Cancellation &cancellation) const;
		qint64 compressedBytes() const;
		qint64 serializedBytes() const;
		qsizetype blockCount() const;

	private:
		struct Data;
		explicit SourceArchive(QSharedPointer<const Data> data);
		friend class QSharedPointer<SourceArchive>;
		QSharedPointer<const Data> m_data;
	};
}
