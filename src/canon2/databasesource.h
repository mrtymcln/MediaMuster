#pragma once

// Projects a database through the established Canon reader, then keeps its
// original bytes in RAM. Restoring records reuses the scan's original receipts.

#include "databaseimage.h"
#include "canon/sourcepipeline.h"
#include "canon/sourcestore.h"
#include <stdexcept>

namespace Canon2
{
	class DatabaseSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class DatabaseSource final : public Canon::SourceStore
	{
	public:
		const DatabaseImage &image() const;
		std::optional<Canon::ParsedSource> restore(const Canon::Cancellation &cancellation) const override;

	private:
		struct Data;
		explicit DatabaseSource(QSharedPointer<const Data> data);
		friend class QSharedPointer<DatabaseSource>;
		friend Canon::PreparedSource prepareDatabase(const Canon::SourceCandidate &, const QString &,
													 const Canon::Cancellation &);
		QSharedPointer<const Data> m_data;
	};

	Canon::PreparedSource prepareDatabase(const Canon::SourceCandidate &candidate, const QString &readReason,
										 const Canon::Cancellation &cancellation);
}
