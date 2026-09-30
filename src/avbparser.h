#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>
#include <atomic>

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
};

struct AvbBin
{
	QString filePath;
	QString displayName;

	/// Different readers use different byte orders, so keep both MobId forms.
	/// Wrapped OMF MobIds only need one.
	QSet<QString> mobIds;
	QVector<AvbMob> mobs;

	/// Filtering needs both valid and complete so we don't miss MobIds.
	/// This doesn't mean every effect or timeline has been checked.
	bool valid = false;
	bool complete = false;
	QString error;
	QStringList warnings;
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
