#pragma once

// Shared file-writing helpers for scanner and OMF tests.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

/// Write `bytes` to `path`, creating parent directories; false on failure.
inline bool tryWriteFile(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	return f.write(bytes) == bytes.size();
}
