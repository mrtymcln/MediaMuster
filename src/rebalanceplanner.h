#pragma once

#include "mediafile.h"
#include "oprequest.h"
#include "rebalanceplan.h"

#include <QHash>
#include <QSet>
#include <optional>

/// Read-only planning and value conversion. File mutations belong to OpRunner.
namespace RebalancePlanner
{
	struct FolderCount
	{
		int count = -1; ///< Unavailable; a known count is zero or greater.
		bool exists = false;
	};

	/// Flat MXF essence counts for the requested managed folders. A zero
	/// count with exists=false means absent under an accessible MXF root.
	/// QDirIterator exposes no enumeration error: accessibility is checked
	/// before and after reading, but this is not an atomic filesystem snapshot.
	QHash<FolderName, FolderCount> countFolders(const QString &mxfRoot, const QSet<FolderName> &folders);

	std::optional<FolderName> parseFolderName(const QString &name);
	std::optional<FolderName> srcFolderOf(const QString &sourcePath);
	bool isEligible(const MediaFile &file);
	RebalancePlan computePlan(const QString &mxfRoot, const QString &volumeLabel,
							  const QVector<MediaFile> &files);
	OpRequest requestForPlan(const RebalancePlan &plan);
} // namespace RebalancePlanner
