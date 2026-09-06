#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// Normalise paths before using them as hash keys. File and directory APIs disagree on
// trailing slashes, `.`/`..`, symlinks/firmlinks (`/var` vs `/private/var`),
// and Unicode form (NFC vs NFD — SMB vs APFS). canonicalFilePath settles all
// of these, but only for a path that exists.
//
// THE RULE THAT MATTERS: the key for a path must not change the moment the
// file appears. So the LEAF is never resolved — only the deepest part of the
// path that already exists is canonicalised, and the rest is re-attached
// verbatim. Keying the whole path through canonicalFilePath broke that:
// a not-yet-created destination got its plain absolute path while the same
// destination, once the first file had landed, got the canonical one. Under
// any symlinked path ("/tmp" -> "/private/tmp", or a user's own symlink to a
// project drive) the two disagree, the runner's claimed-destination lookup
// missed, and the second of two same-named files was silently SKIPPED with
// "a file appeared at this destination after the preview" instead of being
// diverted to "name (2)". Route both insert and lookup through here.

namespace PathKey
{
	inline QString normalise(const QString &path)
	{
		if (path.isEmpty())
			return path;

		// Clean lexically first, so "." and ".." can never survive as leaf
		// components the walk below would re-attach verbatim. canonicalFilePath resolves
		// those through the filesystem only for a path that exists — the
		// very thing that can't be relied on here — so this is the same
		// lexical answer it already gave for anything not yet on disk.
		//
		// Then split into the part we resolve (the ancestors) and the leaf
		// we never do, and walk up until something exists. Everything
		// skipped past is re-attached afterwards, so a destination folder
		// that isn't there yet keys the same as it will once it has been
		// created.
		const QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
		QString head = info.absolutePath();
		QStringList tail{info.fileName()};

		for (;;)
		{
			const QFileInfo dir(head);
			const QString canonical = dir.canonicalFilePath();
			if (!canonical.isEmpty())
			{
				head = canonical;
				break;
			}
			const QString parent = dir.absolutePath();
			// Reached the root (or a drive letter) without finding anything
			// on disk: nothing to canonicalise, use the path as given.
			if (parent.isEmpty() || parent == head)
				break;
			tail.prepend(dir.fileName());
			head = parent;
		}

		QString result = head;
		for (const QString &part : tail)
		{
			if (part.isEmpty()) // a trailing slash contributes no component
				continue;
			if (!result.endsWith(QLatin1Char('/')))
				result += QLatin1Char('/');
			result += part;
		}

		// Preserve "/" but strip trailing slashes from anything else.
		while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
			result.chop(1);

#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
		// Case-fold on the platforms whose default filesystems are
		// case-insensitive, because the canonical form does NOT unify
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
