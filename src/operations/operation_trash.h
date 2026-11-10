#pragma once

#include "operation_file.h"
#include <atomic>

// System Trash only. The coordinator owns journal intent/result and selects
// MediaMuster Trash for network/NEXIS; this adapter never permanently deletes.
// MARK: - System Trash operations

class OperationTrash
{
public:
	enum class Outcome
	{
		Succeeded,
		// On move, native recycling was refused without a move and the source
		// still matches its expected identity. The coordinator requires consent
		// and another recheck before selecting MediaMuster Trash. Restoration
		// can also return this outcome when its native provider is unavailable.
		Unavailable,
		Cancelled,
		Failed
	};
	struct Result
	{
		Outcome outcome = Outcome::Failed;
		QString path;
		QString receipt;
		QString error;
		OperationStamp landed;
	};
	static bool isNetwork(const QString &path);
	static Result move(const QString &path, const OperationStamp &expected,
					   const std::atomic<bool> &cancel);
	// Receipt is opaque provider evidence, not a user-selected source pathname.
	static Result restore(const QString &receipt, const QString &destination,
						  const OperationStamp &expected, const std::atomic<bool> &cancel,
						  const QString &resolvedSource = {});
};

// MARK: - Native Trash interface

namespace OperationTrashPlatform
{
	OperationTrash::Result move(const QString &, const OperationStamp &, const std::atomic<bool> &);
	OperationTrash::Result restore(const QJsonObject &, const QString &, const OperationStamp &,
							const std::atomic<bool> &);
	QString receipt(const QString &provider, const QString &path, const OperationStamp &landed,
					const QByteArray &nativeId = {});
}
