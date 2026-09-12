#pragma once

#include "mediafile.h"
#include "oprequest.h"
#include "rebalanceplan.h"

#include <optional>

/// Read-only planning and value conversion. File mutations belong to OpRunner.
namespace RebalancePlanner
{
std::optional<FolderName> parseFolderName(const QString &name);
std::optional<FolderName> srcFolderOf(const QString &sourcePath);
RebalancePlan computePlan(const QString &mxfRoot, const QString &volumeLabel,
						  const QVector<MediaFile> &files);
OpRequest requestForPlan(const RebalancePlan &plan);
} // namespace RebalancePlanner
