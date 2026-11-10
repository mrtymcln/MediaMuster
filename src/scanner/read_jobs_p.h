#pragma once

#include "mdb_parser.h"
#include "mxf_parser.h"
#include "omf_parser.h"
#include "pmr_parser.h"

#include <QThreadPool>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <variant>

namespace Bella
{
	class ScanControl;

	namespace Detail
	{
		// MARK: - Read requests and replies

		enum class ReadKind
		{
			Pmr,
			Mdb,
			Mxf,
			Omf
		};

		struct ReadTag
		{
			ReadKind kind;
			qsizetype index;
		};

		using ReadReport = std::variant<PmrReport, MdbReport, MxfReport, OmfReport, std::exception_ptr>;

		struct ReadReply
		{
			ReadTag tag;
			ReadReport report;
		};

		// MARK: - Bounded read jobs

		// One organiser submits and collects. The scan control outlives the pool.
		class ReadJobs
		{
		public:
			static constexpr int kMaxOutstanding = 4;

			explicit ReadJobs(ScanControl &control);
			[[nodiscard]] bool submit(ReadTag tag, std::function<ReadReport()> read);
			// Requires outstanding() > 0, including when draining after Cancel.
			[[nodiscard]] ReadReply take();
			[[nodiscard]] int outstanding() const noexcept;

		private:
			void publish(ReadReply reply) noexcept;

			ScanControl &m_control;
			std::array<std::optional<ReadReply>, kMaxOutstanding> m_replies;
			std::size_t m_firstReply = 0;
			std::size_t m_replyCount = 0;
			int m_outstanding = 0;
			// Destroy the pool and join its workers before destroying the inbox.
			QThreadPool m_pool;
		};
	}
}
