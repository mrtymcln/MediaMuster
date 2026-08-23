#pragma once

#include <QIcon>
#include <QString>

namespace Icons
{

	/// Volume sidebar.
	/// For a non-network `volumeType`, `path` (when given) is sniffed via
	/// QStorageInfo::fileSystemType() so a network mount still gets the
	/// network icon.
	QIcon forVolumeType(const QString &volumeType, const QString &path = {});

	/// Project sidebar / summary: a real project, or the one "No project" row.
	QIcon forProject(bool hasProject);

} // namespace Icons