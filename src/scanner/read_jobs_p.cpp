#include "read_jobs_p.h"

#include "bella_scanner.h"

#include <QMutex>
#include <type_traits>
#include <utility>

namespace Bella::Detail
{
	static_assert(std::is_nothrow_move_constructible_v<ReadReply>);

	// MARK: - Read job lifecycle

	ReadJobs::ReadJobs(ScanControl &control) : m_control(control)
	{
		m_pool.setMaxThreadCount(kMaxOutstanding);
	}

	// MARK: - Submission and results

	bool ReadJobs::submit(ReadTag tag, std::function<ReadReport()> read)
	{
		// Serialize accepting work with Cancel. Workers only lock to publish.
		const QMutexLocker lock(&m_control.m_mutex);
		if (m_control.cancelled() || m_outstanding == kMaxOutstanding)
			return false;

		m_pool.start([this, tag, read = std::move(read)]
					 {
			try
			{
				publish({tag, read()});
			}
			catch (...)
			{
				publish({tag, std::current_exception()});
			} });
		++m_outstanding;
		return true;
	}

	ReadReply ReadJobs::take()
	{
		Q_ASSERT(m_outstanding > 0);
		const QMutexLocker lock(&m_control.m_mutex);
		// Accepted work still owes a reply after Cancel.
		while (m_replyCount == 0)
			m_control.m_ready.wait(&m_control.m_mutex);

		auto &slot = m_replies[m_firstReply];
		ReadReply reply = std::move(*slot);
		slot.reset();
		m_firstReply = (m_firstReply + 1) % m_replies.size();
		--m_replyCount;
		--m_outstanding;
		return reply;
	}

	int ReadJobs::outstanding() const noexcept
	{
		return m_outstanding;
	}

	void ReadJobs::publish(ReadReply reply) noexcept
	{
		const QMutexLocker lock(&m_control.m_mutex);
		Q_ASSERT(m_replyCount < m_replies.size());
		const auto index = (m_firstReply + m_replyCount) % m_replies.size();
		m_replies[index].emplace(std::move(reply));
		++m_replyCount;
		m_control.m_ready.wakeOne();
	}
}
