#pragma once

#include "avbfilereferences.h"

#include <QString>
#include <QStringList>
#include <QMetaType>
#include <atomic>

struct AvbBin
{
	QString filePath;
	QString displayName;

	/// Full file IDs and explicitly legacy references from MSML locators.
	AvbFileReferences mediaFileIds;

	/// Readability and reference coverage are separate. Partial results remain
	/// usable with persistent warnings; empty reference sets create no operand.
	bool usable = false;
	bool coverageComplete = false;
	QString error;
	QStringList warnings;

	/// Usable for filtering; loading state belongs to the dialog.
	[[nodiscard]] bool isUsable() const noexcept { return usable; }
};

Q_DECLARE_METATYPE(AvbBin)

struct AvbHeaderResult
{
	bool recognized = false;
	QString error;
};

class AvbBinLoader
{
public:
	/// Keep the header check quick for dragging. The full bin still needs loading.
	[[nodiscard]] static AvbHeaderResult inspectHeader(const QString &avbFilePath);

	/// The loader borrows this flag, so keep it alive until loading finishes.
	[[nodiscard]] static AvbBin load(
		const QString &avbFilePath, const std::atomic_bool *cancelled = nullptr);
};
