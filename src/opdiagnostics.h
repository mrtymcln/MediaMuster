#pragma once

#include "nativefile.h"
#include <QJsonObject>
#include <QStringList>
#include <atomic>
#include <functional>

// A disposable-files harness around the production engine. It never moves or
// deletes a selected sample. All failure injection is explicitly scoped here.
namespace OpDiagnostics
{
struct Options
{
	QString sourceArea;
	QString destinationArea;
	QString reportArea;
	QString storageNotes;
	QStringList samples;
	// Explicit injection for regression tests; the UI uses the native implementation.
	NativeFile::DirectorySync directorySync = NativeFile::syncDirectory;
};
struct Report
{
	QJsonObject json;
	QString text;
	QString path;
};
using Progress = std::function<void(const QString &)>;
QStringList bundledSamples();
Report run(const Options &options, const std::atomic<bool> &cancel, const Progress &progress = {});
} // namespace OpDiagnostics
