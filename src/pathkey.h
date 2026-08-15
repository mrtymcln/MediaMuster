#pragma once

#include <QFileInfo>
#include <QString>

// Normalise paths before using them as hash keys. Qt APIs disagree on
// trailing slashes, `.`/`..`, symlinks/firmlinks (`/var` vs `/private/var`),
// and Unicode form (NFC vs NFD — SMB vs APFS). canonicalFilePath settles all
// of these, but only for a path that exists; a not-yet-created path falls back
// to absoluteFilePath, which just makes it absolute. Route both insert and
// lookup through PathKey::normalise so the two sides stay aligned.

namespace PathKey
{
	// Falls back to absoluteFilePath for paths that don't exist yet
	// (canonicalFilePath returns empty in that case).
	inline QString normalise(const QString &path)
	{
		if (path.isEmpty())
			return path;

		const QFileInfo fi(path);
		QString result = fi.canonicalFilePath();
		if (result.isEmpty())
			result = fi.absoluteFilePath();

		// Preserve "/" but strip trailing slashes from anything else.
		while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
			result.chop(1);

		return result;
	}
} // namespace PathKey