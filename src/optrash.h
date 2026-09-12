#pragma once

#include "opfile.h"
#include <atomic>

// System Trash only. The coordinator owns journal intent/result and selects
// MediaMuster Trash for network/NEXIS; this adapter never permanently deletes.
class OpTrash
{
  public:
	enum class Outcome { Succeeded, Unavailable, Cancelled, Failed };
	struct Result
	{
		Outcome outcome = Outcome::Failed;
		QString path;
		QString receipt;
		QString error;
		OpStamp landed;
	};
	static bool isNetwork(const QString &path);
	static Result move(const QString &path, const OpStamp &expected,
		const std::atomic<bool> &cancel);
	// Receipt is opaque provider evidence, not a user-selected source pathname.
	static Result restore(const QString &receipt, const QString &destination,
		const OpStamp &expected, const std::atomic<bool> &cancel,
		const QString &resolvedSource = {});
};

namespace OpTrashPlatform
{
OpTrash::Result move(const QString &, const OpStamp &, const std::atomic<bool> &);
OpTrash::Result restore(const QJsonObject &, const QString &, const OpStamp &,
	const std::atomic<bool> &);
QString receipt(const QString &provider, const QString &path, const OpStamp &landed,
	const QByteArray &nativeId = {});
}
