#pragma once

// Shared byte-level file helpers for the suite — six tests used to carry
// their own spelling of each of these. Sits beside testbento.h, the
// suite's other shared fixture.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTest>

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

/// Asserting twin: fails the test on error. (As with the member versions
/// this replaces, a QVERIFY failure inside a helper aborts the helper and
/// records the failure; the calling test function still continues.)
inline void writeFile(const QString &path, const QByteArray &contents)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	QVERIFY2(f.open(QIODevice::WriteOnly),
			 qPrintable(QStringLiteral("failed to create %1: %2").arg(path, f.errorString())));
	QCOMPARE(f.write(contents), qint64(contents.size()));
}

/// dir + name convenience returning the path written; empty on failure.
inline QString writeFileIn(const QString &dir, const QString &name, const QByteArray &contents)
{
	const QString path = dir + QLatin1Char('/') + name;
	return tryWriteFile(path, contents) ? path : QString();
}

inline QByteArray readFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return f.readAll();
}

/// Hand-write a JSON-lines journal file ('\n' after every line), the way
/// the recovery tests build scenarios without depending on the writer
/// under test. Returns the path; empty on failure.
inline QString writeJournal(const QString &dir, const QString &name, const QStringList &lines)
{
	const QString path = dir + QLatin1Char('/') + name;
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return {};
	for (const QString &line : lines)
	{
		f.write(line.toUtf8());
		f.write("\n");
	}
	return path;
}
