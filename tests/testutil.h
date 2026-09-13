#pragma once

// Shared file-writing helpers for scanner and OMF tests.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

/// Write `bytes` to `path`, creating parent directories. False on any
/// failure; for call sites that check the result themselves.
inline bool tryWriteFile(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	return f.write(bytes) == bytes.size();
}

/// dir + name convenience returning the path written; empty on failure.
inline QString writeFileIn(const QString &dir, const QString &name, const QByteArray &contents)
{
	const QString path = dir + QLatin1Char('/') + name;
	return tryWriteFile(path, contents) ? path : QString();
}
