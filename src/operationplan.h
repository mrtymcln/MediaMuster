#pragma once

#include "oprequest.h"
#include <functional>

// Shared advisory decisions for previews and execution. These results do not
// reserve paths or authorize mutations; the runner rechecks live storage.
namespace OperationPlan
{
QString destinationPath(const QString &name, const QString &folder, const QString &root,
	bool preserve, bool omfEra = false);
std::optional<QString> findKeepBothPath(const QString &path);
bool sameVolumeForRename(const QString &source, const QString &destination);
bool alreadyAtDestination(const QString &source, const QString &destination);

struct CopyMoveAssessment
{
	bool copyThenRemove = false;
	qint64 temporaryBytes = 0;
};
using CanRelocate = std::function<bool(const QString &, const QString &)>;
using SameFile = std::function<bool(const QString &, const QString &)>;
// When any required Move item needs copying, every required item is copied
// before any original is removed. Explicit skips and identity-confirmed
// files already at their destination contribute no extra bytes.
// The default probe is read-only. Execution supplies fresh capability evidence.
CopyMoveAssessment assessCopyMove(const OpRequest &request,
	const CanRelocate &canRelocate = sameVolumeForRename, bool forceCopy = false,
	const SameFile &sameFile = alreadyAtDestination);
}
