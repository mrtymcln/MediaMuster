#pragma once

#include "bella_scanner.h"

#include <QSet>

namespace Bella::Detail
{
	// MARK: - Database discovery

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
