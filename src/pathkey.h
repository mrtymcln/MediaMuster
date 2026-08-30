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

#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
		// Case-fold on the platforms whose default filesystems are
		// case-insensitive, because canonicalFilePath does NOT unify
		// casing everywhere: macOS resolves "clip.mxf" to the on-disk
		// "CLIP.mxf", but Windows keeps the caller's spelling — so two
		// spellings of ONE on-disk file produced two different keys, the
		// claimed-set lookup missed, and a case-variant flatten silently
		// SKIPPED the second file (first Windows CI run, 2026-08-30).
		// The fold is safe for every current user of these keys: on a
		// rare case-SENSITIVE volume it can at worst cause an
		// unnecessary " (2)" divert name — never an overwrite, which is
		// prevented by parking and NewOnly, not by this key.
		result = result.toCaseFolded();
#endif

		return result;
	}
} // namespace PathKey