#include "bella_scanner.h"

#include "bella_scanner_p.h"
#include "discovery_p.h"
#include "read_jobs_p.h"

#include <QFileInfo>
#include <algorithm>
#include <utility>

namespace Bella
{
	// MARK: - Cancellation

	void ScanControl::cancel()
	{
		const QMutexLocker lock(&m_mutex);
		m_stopRequested.store(true, std::memory_order_release);
		m_ready.wakeAll();
	}

	bool ScanControl::cancelled() const noexcept
	{
		return m_stopRequested.load(std::memory_order_acquire);
	}

	const std::atomic_bool &ScanControl::stopRequested() const noexcept
	{
		return m_stopRequested;
	}

	// MARK: - Physical file checks

	Detail::PhysicalCheck Detail::comparePhysical(const ScanEntry &entry,
												  const PhysicalObservation &observed)
	{
		const bool sizes = entry.sizeBytes && observed.sizeBytes &&
						   *entry.sizeBytes >= 0 && *observed.sizeBytes >= 0;
		const bool dates = entry.modified.isValid() && observed.modified.isValid();
		if (observed.presence == PhysicalPresence::Replaced ||
			(sizes && entry.sizeBytes != observed.sizeBytes) ||
			(dates && entry.modified != observed.modified))
			return PhysicalCheck::Changed;
		if (observed.presence != PhysicalPresence::File)
			return PhysicalCheck::Unavailable;
		return sizes && dates ? PhysicalCheck::Verified : PhysicalCheck::Unverified;
	}

	namespace
	{
		using Detail::PhysicalCheck;
		using Detail::PhysicalObservation;
		using Detail::PhysicalPresence;
		using Detail::ReadJobs;
		using Detail::ReadKind;
		using Detail::ReadReply;
		using Detail::ReadReport;
		using Detail::ReadTag;

		// MARK: - Scan reporting and physical observations

		void reportProblem(ScanResult &result, const ScanCallbacks &callbacks,
						   const QString &path, const QString &reason)
		{
			result.problems.append({path, reason});
			if (callbacks.warning)
				callbacks.warning(result.problems.last());
		}

		void reportNotes(ScanResult &result, const ScanCallbacks &callbacks,
						 const QString &path, const QStringList &notes)
		{
			for (const auto &note : notes)
				reportProblem(result, callbacks, path, note);
		}

		PhysicalObservation observePhysical(const QString &path)
		{
			const QFileInfo info(path);
			if (info.isSymLink())
				return {PhysicalPresence::Replaced, {}, {}};
			if (!info.exists())
				return {};
			if (!info.isFile())
				return {PhysicalPresence::Replaced, {}, {}};
			const auto size = info.size();
			return {PhysicalPresence::File, size >= 0 ? std::optional<qint64>(size) : std::nullopt,
					info.lastModified()};
		}

		bool qualifyPhysical(ScanEntry &entry, const QString &checkpoint, ScanResult &result,
							 const ScanCallbacks &callbacks)
		{
			const auto observed = observePhysical(entry.path);
			switch (Detail::comparePhysical(entry, observed))
			{
			case PhysicalCheck::Verified:
				return true;
			case PhysicalCheck::Unverified:
			{
				QStringList missing;
				if (!entry.sizeBytes || !observed.sizeBytes)
					missing.append(QStringLiteral("size"));
				if (!entry.modified.isValid() || !observed.modified.isValid())
					missing.append(QStringLiteral("modification date"));
				reportProblem(result, callbacks, entry.path,
							  QStringLiteral("Physical-file recheck %1 was unverified. Missing %2.")
								  .arg(checkpoint, missing.join(QStringLiteral(" and "))));
				return true;
			}
			case PhysicalCheck::Changed:
				entry.operationSafety = RescanReason::FileChanged;
				reportProblem(result, callbacks, entry.path,
							  QStringLiteral("Physical file changed after discovery (%1).").arg(checkpoint));
				return false;
			case PhysicalCheck::Unavailable:
				entry.operationSafety = RescanReason::FilePresenceUnconfirmed;
				reportProblem(result, callbacks, entry.path,
							  QStringLiteral("Could not confirm that the physical file is still present %1.")
								  .arg(checkpoint));
				return false;
			}
			Q_UNREACHABLE();
		}

		// MARK: - Source reading

		ReadReport readSource(ReadKind kind, const QString &path, const ScanControl &control,
							  bool includePrecomputeDetails)
		{
			const auto &stop = control.stopRequested();
			switch (kind)
			{
			case ReadKind::Pmr:
				return parsePmr(path, stop);
			case ReadKind::Mdb:
				return parseMdb(path, stop, includePrecomputeDetails);
			case ReadKind::Mxf:
				return parseMxf(path, stop, includePrecomputeDetails);
			case ReadKind::Omf:
				return parseOmf(path, stop, includePrecomputeDetails);
			}
			Q_UNREACHABLE();
		}

		QString failureReason(const std::exception_ptr &failure)
		{
			try
			{
				std::rethrow_exception(failure);
			}
			catch (const std::exception &error)
			{
				return QStringLiteral("Scan failed. %1").arg(QString::fromUtf8(error.what()));
			}
			catch (...)
			{
				return QStringLiteral("Scan failed with an unexpected error.");
			}
		}

		template <typename TagAt, typename PathFor, typename Collect>
		void readStage(ReadJobs &jobs, ScanStage stage, qsizetype count, TagAt tagAt, PathFor pathFor, Collect collect,
					   const ScanRequest &request, ScanControl &control, ScanResult &result,
					   const ScanCallbacks &callbacks)
		{
			if (count > 0 && !control.cancelled() && callbacks.stageProgress)
				callbacks.stageProgress(stage, 0, count, {});
			qsizetype submitted = 0;
			qsizetype completed = 0;
			for (;;)
			{
				while (submitted < count && jobs.outstanding() < ReadJobs::kMaxOutstanding && !control.cancelled())
				{
					const ReadTag tag = tagAt(submitted);
					const QString path = pathFor(tag);
					if (callbacks.progress)
						callbacks.progress(submitted, count, path);
					if (!jobs.submit(tag, [kind = tag.kind, path, &control, details = request.includePrecomputeDetails]
									 { return readSource(kind, path, control, details); }))
						break;
					++submitted;
				}
				if (jobs.outstanding() == 0)
					return;
				auto reply = jobs.take();
				const auto tag = reply.tag;
				if (const auto *failure = std::get_if<std::exception_ptr>(&reply.report))
				{
					result.outcome = ScanOutcome::Failed;
					control.cancel();
					reportProblem(result, callbacks, pathFor(tag), failureReason(*failure));
				}
				else
					collect(std::move(reply));
				++completed;
				if (callbacks.stageProgress)
					callbacks.stageProgress(stage, completed, count, pathFor(tag));
			}
		}

		void readDatabases(ReadJobs &jobs, Detail::DiscoveredDatabases &databases,
						   const ScanRequest &request, ScanControl &control, ScanResult &result,
						   const ScanCallbacks &callbacks)
		{
			const auto pmrCount = databases.pmrs.size();
			readStage(jobs, ScanStage::ReadingDatabases, pmrCount + databases.mdbs.size(), [pmrCount](qsizetype index)
					  { return index < pmrCount ? ReadTag{ReadKind::Pmr, index} : ReadTag{ReadKind::Mdb, index - pmrCount}; }, [&](ReadTag tag) -> const QString &
					  { return tag.kind == ReadKind::Pmr ? databases.pmrs.at(tag.index).source.path : databases.mdbs.at(tag.index).path; }, [&](ReadReply reply)
					  {
						  if (reply.tag.kind == ReadKind::Pmr)
						  {
							  auto &report = std::get<PmrReport>(reply.report);
							  reportNotes(result, callbacks, report.source.path, report.diagnostics);
							  databases.pmrs[reply.tag.index] = std::move(report);
						  }
						  else
						  {
							  auto &report = std::get<MdbReport>(reply.report);
							  reportNotes(result, callbacks, report.path, report.problemNotes);
							  databases.mdbs[reply.tag.index] = std::move(report);
						  } }, request, control, result, callbacks);
		}

		// MARK: - Header matching

		bool changed(const MxfReport &report)
		{
			return report.reading == MxfReadResult::Changed || report.sizeDateCheck == MxfCheckResult::Changed;
		}

		bool changed(const OmfReport &report)
		{
			return report.sizeDateCheck == OmfCheckResult::Changed;
		}

		void verifyFileIdWhenAvailable(ScanEntry &entry, const std::optional<MobId> &id, FileIdOrigin origin)
		{
			if (!id)
				return;
			const auto bytes = id->bytes();
			if (std::any_of(bytes.begin(), bytes.end(), [](char byte)
							{ return byte != '\0'; }))
				entry.operationSafety = VerifyFileId{*id, origin};
		}

		template <typename Report>
		void collectHeader(const Report &report, ScanEntry &entry, const MatchContext &context,
						   const ScanRequest &request, ScanControl &control, ScanResult &result,
						   const ScanCallbacks &callbacks)
		{
			// A header attempt replaces the skipped-header verification instruction.
			entry.operationSafety = CheckPhysicalFile{};
			const bool headerChanged = changed(report);
			if (headerChanged)
				entry.operationSafety = RescanReason::FileChanged;
			reportNotes(result, callbacks, report.path, report.problemNotes);
			if (control.cancelled() && !headerChanged)
				return; // The first qualified match survives a skipped recheck.

			const bool eligible = !headerChanged &&
								  qualifyPhysical(entry, QStringLiteral("after reading its header"), result, callbacks);
			if (eligible)
				verifyFileIdWhenAvailable(entry, report.fileInformation.fileMobId, FileIdOrigin::Header);
			entry.match = matchFile(context, entry.path, eligible, std::cref(report),
									request.includePrecomputeDetails);
		}

		void matchEntries(ReadJobs &jobs, const MatchContext &context, const ScanRequest &request,
						  ScanControl &control, ScanResult &result, const ScanCallbacks &callbacks)
		{
			QVector<ReadTag> headers;
			for (qsizetype index = 0; index < result.entries.size(); ++index)
			{
				auto &entry = result.entries[index];
				const bool eligible = !control.cancelled() &&
									  qualifyPhysical(entry, QStringLiteral("before matching"), result, callbacks);
				entry.match = matchFile(context, entry.path, eligible, {}, request.includePrecomputeDetails);
				if (eligible)
					verifyFileIdWhenAvailable(entry, entry.match.metadata.fileMobId, FileIdOrigin::Database);
				if (entry.match.needsHeader && !control.cancelled())
					headers.append({entry.omfEra ? ReadKind::Omf : ReadKind::Mxf, index});
			}

			readStage(jobs, ScanStage::ReadingHeaders, headers.size(), [&](qsizetype index)
					  { return headers.at(index); }, [&](ReadTag tag) -> const QString &
					  { return result.entries.at(tag.index).path; }, [&](ReadReply reply)
					  {
						  auto &entry = result.entries[reply.tag.index];
						  if (reply.tag.kind == ReadKind::Mxf)
							  collectHeader(std::get<MxfReport>(reply.report), entry, context, request,
											control, result, callbacks);
						  else
							  collectHeader(std::get<OmfReport>(reply.report), entry, context, request,
											control, result, callbacks); }, request, control, result, callbacks);
		}
	}

	// MARK: - Scan coordination

	ScanResult scan(const ScanRequest &request, ScanControl &control, const ScanCallbacks &callbacks)
	{
		ScanResult result;
		try
		{
			Detail::DiscoveredDatabases databases;
			Detail::discover(request, control, result, databases, callbacks);
			ReadJobs jobs(control);
			try
			{
				readDatabases(jobs, databases, request, control, result, callbacks);
				if (!control.cancelled() && callbacks.stageProgress)
					callbacks.stageProgress(ScanStage::CheckingFiles, 0, 0, {});
				const auto context = prepareMatchContext(std::move(databases.pmrs), std::move(databases.mdbs),
														 std::move(databases.completedFolders));
				matchEntries(jobs, context, request, control, result, callbacks);
				if (!control.cancelled() && callbacks.finalising)
					callbacks.finalising();
			}
			catch (...)
			{
				control.cancel();
				while (jobs.outstanding() != 0)
					(void)jobs.take();
				throw;
			}
		}
		catch (...)
		{
			result.outcome = ScanOutcome::Failed;
			result.problems.append({{}, failureReason(std::current_exception())});
		}
		if (result.outcome == ScanOutcome::Completed && control.cancelled())
			result.outcome = ScanOutcome::Cancelled;
		return result;
	}
}
