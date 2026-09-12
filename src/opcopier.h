#pragma once
#include "opfile.h"
#include <atomic>
#include <functional>

class OpCopier
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
		QString hash;
		QString error;
		bool durable = false;
		bool metadataComplete = false;
		bool retryable = false;
	};
	using Progress = std::function<void(qint64, qint64, bool)>;
	Result copy(OpFile &source, OpFile &destination, const std::atomic<bool> &cancel,
				const Progress &progress = {}, const std::function<void()> &beforeReadback = {},
				bool verify = true);
	static Result hash(OpFile &file, const std::atomic<bool> &cancel,
					   const Progress &progress = {});
};
