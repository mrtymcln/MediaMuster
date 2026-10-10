#pragma once

#include "binfilereferences.h"
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
	struct AvbResolution;
}

struct AvbMob
{
	static constexpr int masterMobType = 2;

	/// Keep material fields little-endian so the MobId stays the same across AVB byte orders.
	QString mobId;
	QString name;

	/// Clips can move between bins, so use the original bin recorded in _ORG_BIN.
	QString originalBin;
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
	QVector<AvbMob> mobs;

	/// Full file IDs and explicitly legacy references from MSML locators.
	BinFileReferences mediaFileIds;

	/// Shared source evidence survives worker delivery and filter snapshots.
	QSharedPointer<const MediaEngine::ParsedSource> source;
	QSharedPointer<const MediaEngine::AvbResolution> resolution;

	/// Readability and reference coverage are separate. Partial results remain
	/// usable with persistent warnings; empty reference sets create no operand.
	bool valid = false;
	bool complete = false;
	QString error;
	QStringList warnings;

	/// Usable for filtering and metadata; loading state belongs to the dialog.
	[[nodiscard]] bool isUsable() const noexcept { return valid; }
};

Q_DECLARE_METATYPE(AvbBin)

struct AvbHeaderCheck
{
	bool recognized = false;
	QString error;
};

class AvbParser
{
public:
	/// Keep the header check quick for dragging. The full bin still needs parsing.
	[[nodiscard]] static AvbHeaderCheck inspectHeader(const QString &avbFilePath);

	/// The parser borrows this flag, so keep it alive until parsing finishes.
	[[nodiscard]] static AvbBin parse(
		const QString &avbFilePath, const std::atomic_bool *cancelled = nullptr);
};
