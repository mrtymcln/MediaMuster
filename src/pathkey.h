#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// Normalise paths for hash keys by cleaning "."/".." and trailing slashes,
// and resolving existing ancestors through canonicalFilePath. The leaf is
// not resolved, and Unicode normalization forms (NFC/NFD) are not unified.
//
// Keys stay stable when a destination file is created, including beneath a
// symlinked parent. Resolve the deepest existing ancestor, then append the
// unresolved components and leaf. Use this for both insertion and lookup.

namespace PathKey
{
	inline QString normalise(const QString &path)
	{
		if (path.isEmpty())
			return path;

		// Remove "." and ".." even for paths that do not exist, then resolve
		// ancestors without resolving the leaf.
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
		// Canonical paths do not reliably unify case on these platforms.
		// Folding may cause an unnecessary keep-both name on case-sensitive
		// volumes. File identities and native no-overwrite operations protect files.
		result = result.toCaseFolded();
#endif

		return result;
	}
} // namespace PathKey
