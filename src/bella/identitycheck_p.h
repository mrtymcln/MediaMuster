#pragma once

#include "mobid.h"
#include "omfuid.h"

#include <QFile>
#include <QScopeGuard>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace Bella::Detail
{
	// Temporary operation facts; never added to the scanner's reader reports.
	struct FileIdentity
	{
		QString fileId;
		QSet<QString> masterIds;

		bool matches(const QString &expectedFileId, const QStringList &expectedMasters) const
		{
			return !fileId.isEmpty() && (expectedFileId.isEmpty() || expectedFileId == fileId) &&
				   std::all_of(expectedMasters.cbegin(), expectedMasters.cend(),
							   [&](const QString &id)
							   { return masterIds.contains(id); });
		}
	};

	inline QString identityText(const MobId &id) { return OmfUid::toIdText(id.bytes()); }

	// Borrow the caller's actual opened file, including native-handle protection.
	// No path lookup, reopening, adoption or closing takes place here.
	template <class ReadIdentity>
	bool checkOpenedIdentity(QFile &file, const QString &expectedFileId,
							 const QStringList &expectedMasters, ReadIdentity readIdentity)
	{
		if (!file.isOpen() || !file.isReadable() || file.isSequential() || file.isTextModeEnabled())
			return false;
		const auto position = file.pos();
		if (position < 0)
			return false;
		const auto restore = qScopeGuard([&]
										 { file.seek(position); });
		return file.seek(0) && readIdentity().matches(expectedFileId, expectedMasters);
	}
}
