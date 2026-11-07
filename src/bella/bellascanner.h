#pragma once

#include "matching.h"

#include <QDateTime>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <functional>
#include <variant>

namespace Bella
{
	namespace Detail
	{
		class ReadJobs;
	}

	struct ScanLocation
	{
		enum class Kind
		{
			MediaBase,
			RequiredMediaBase,
			MediaTree
		};
		Kind kind;
		QString path;
	};

	struct ScanRequest
	{
		QVector<ScanLocation> locations;
		bool includeOmf = false;
		bool includePrecomputeDetails = false;
	};

	struct CheckPhysicalFile
	{
	};

	enum class FileIdOrigin
	{
		Database,
		Header
	};

	struct VerifyFileId
	{
		MobId id;
		FileIdOrigin origin;
	};

	enum class RescanReason
	{
		FileChanged,
		FilePresenceUnconfirmed
	};

	// Live physical checks still apply when a full ID also needs verification.
	using OperationSafety = std::variant<CheckPhysicalFile, VerifyFileId, RescanReason>;

	struct ScanEntry
	{
		quint64 kelpieId = 0;
		QString path;
		std::optional<qint64> sizeBytes;
		QDateTime created;
		QDateTime modified;
		QString volumeIdentifier;
		QString volumePath;
		QString volumeName;
		bool omfEra = false;
		bool quarantined = false;
		MatchResult match;
		OperationSafety operationSafety;
	};

	struct ScanProblem
	{
		QString path;
		QString reason;
	};

	enum class ScanOutcome
	{
		Completed,
		Cancelled,
		Failed
	};

	struct ScanResult
	{
		QVector<ScanEntry> entries;
		QVector<ScanProblem> problems;
		ScanOutcome outcome = ScanOutcome::Completed;
		bool discoveryComplete = false;
	};

	enum class ScanStage
	{
		ReadingDatabases,
		CheckingFiles,
		ReadingHeaders
	};

	// Callbacks run on the organiser. progress precedes each attempted submission.
	// stageProgress counts collected replies; a zero total marks an indeterminate stage.
	struct ScanCallbacks
	{
		std::function<void(const QString &)> discovering;
		std::function<void(qsizetype, qsizetype, const QString &)> progress;
		std::function<void()> finalising;
		std::function<void(const ScanProblem &)> warning;
		std::function<void(ScanStage, qsizetype, qsizetype, const QString &)> stageProgress;
	};

	// One fresh control per scan. The caller keeps it alive until scan returns.
	class ScanControl
	{
	public:
		void cancel();
		[[nodiscard]] bool cancelled() const noexcept;
		[[nodiscard]] const std::atomic_bool &stopRequested() const noexcept;

	private:
		friend class Detail::ReadJobs;
		std::atomic_bool m_stopRequested{false};
		QMutex m_mutex;
		QWaitCondition m_ready;
	};

	// Run on the organiser thread. The caller serializes scan invocations.
	[[nodiscard]] ScanResult scan(const ScanRequest &request, ScanControl &control,
								  const ScanCallbacks &callbacks = {});
}
