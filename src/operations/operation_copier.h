#pragma once
#include "operation_file.h"
#include <atomic>
#include <functional>

// MARK: - Protected file copying

class OperationCopier
{
public:
	enum class Outcome
	{
		Succeeded,
		Cancelled,
		Failed
	};
	struct Result
	{
		Outcome outcome = Outcome::Failed;
		QString error;
		bool durable = false;
		bool metadataComplete = false;
		bool retryable = false;
	};
	using Progress = std::function<void(qint64, qint64)>;
	Result copy(OperationFile &source, OperationFile &destination, const std::atomic<bool> &cancel,
				const Progress &progress = {});
	// Native transfer errors only; identity, metadata and durability failures never retry.
	static bool isRetryableNativeError(int error);
};
