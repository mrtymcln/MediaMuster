#pragma once

#include "bellascanner.h"

#include <QSet>

namespace Bella::Detail
{
	struct DiscoveredDatabases
	{
		QVector<PmrReport> pmrs;
		QVector<MdbReport> mdbs;
		QSet<QString> completedFolders;
	};

	// Capture physical entries and the paths of databases awaiting reader work.
	void discover(const ScanRequest &, ScanControl &, ScanResult &, DiscoveredDatabases &,
				  const ScanCallbacks &);
}
