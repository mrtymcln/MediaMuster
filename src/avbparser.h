#pragma once

#include <QSet>
#include <QString>

// MARK: - AvbBin

/// The Avid bin stripped down to just its MOB IDs — this is all the Bin Filter cares about.
struct AvbBin
{
	QString filePath;
	QString displayName;

	/// Matches `MobId::format` — four 16-char hex chunks, dot-separated, lowercase.
	/// Grabs SMPTE (`06 0E 2B 34 ...`), Avid (`06 0A 2B 34 ...`), and their
	/// byte-swapped twins. Gory details in avbparser.cpp.
	QSet<QString> mobIds;

	bool valid = false;
};

// MARK: - AvbParser

/// Cracks open an Avid bin. If anything goes sideways, you get an empty AvbBin with `valid=false`.
class AvbParser
{
public:
	static AvbBin parse(const QString &avbFilePath);
};