#pragma once

#include "avbfilereferences.h"
#include "mediaevidence.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>
#include <atomic>
#include <QSharedPointer>

namespace MediaEngine
{
	struct ParsedSource;
	struct AvbReferenceResult;
}

struct AvbComposition
{
	static constexpr int masterMobType = 2;

	/// Keep material fields little-endian so the MobId stays the same across AVB byte orders.
	QString mobId;
	QString name;

	/// Clips can move between bins, so use the original bin recorded in _ORG_BIN.
	QString originalBinName;
	QString originalBinUid;
	int mobType = 0;
	int usageCode = 0;
	QVector<MetadataObservation> nameObservations;
	QVector<MetadataObservation> originalBinObservations;
};

struct AvbBin
{
	QString filePath;
	QString displayName;

	/// Composition identities and clip/original-bin metadata.
	QVector<AvbComposition> compositions;

	/// Full file IDs and explicitly legacy references from MSML locators.
	AvbFileReferences mediaFileIds;

	/// Shared source evidence survives worker delivery and filter snapshots.
	QSharedPointer<const MediaEngine::ParsedSource> sourceGraph;
	QSharedPointer<const MediaEngine::AvbReferenceResult> referenceResult;

	/// Readability and reference coverage are separate. Partial results remain
	/// usable with persistent warnings; empty reference sets create no operand.
	bool usable = false;
	bool coverageComplete = false;
	QString error;
	QStringList warnings;

	/// Usable for filtering and metadata; loading state belongs to the dialog.
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
