#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMetaType>
#include <atomic>

/// Metadata owned by a CMPO object. originalBin is the recorded _ORG_BIN,
/// while the current AVB path is retained separately on AvbBin.
struct AvbMob
{
	static constexpr int masterMobType = 2;

	/// Material scalar fields use little-endian bytes, regardless of file order.
	QString mobId;
	QString name;
	QString originalBin;
	QString originalBinUid;
	int mobType = 0;
	int usageCode = 0;
};

struct AvbBin
{
	QString filePath;
	QString displayName;

	/// Typed object identities and references. Both material byte orders are
	/// retained for compatibility with the existing media readers.
	QSet<QString> mobIds;
	QVector<AvbMob> mobs;

	/// Framing and understood properties are valid. complete additionally
	/// requires supported whole-bin identity coverage, not timeline evaluation.
	bool valid = false;
	bool complete = false;
	QString error;
	QStringList warnings;
};
Q_DECLARE_METATYPE(AvbBin)

/// Result of recognising an AVB document signature without parsing its body.
struct AvbHeaderCheck
{
	bool recognized = false;
	QString error;
};

// MARK: - AvbParser

/// Bounded AVB object reader. Malformed or cancelled input clears identities.
/// Consumers must require valid && complete before applying a bin filter.
class AvbParser
{
public:
	/// Recognises the 21-byte AVB document signature in either byte order.
	/// This bounded probe does not validate the document body or file extension.
	[[nodiscard]] static AvbHeaderCheck inspectHeader(const QString &avbFilePath);

	/// cancelled is an optional observer; its owner keeps it alive until parse returns.
	[[nodiscard]] static AvbBin parse(
		const QString &avbFilePath, const std::atomic_bool *cancelled = nullptr);
};
