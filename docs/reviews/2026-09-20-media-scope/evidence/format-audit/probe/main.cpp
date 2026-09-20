#include "mediascanner.h"
#include "omfparser.h"
#include "conventions.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDebug>

static const QString fixtures = QStringLiteral("/Users/martymclean/Developer/MediaMuster/tests/fixtures/");
static const QString wav = QStringLiteral("TONE_100A01.6A972974.039700.wav");
static const QString aif = QStringLiteral("TONE_100A01.6A972997.0C53E0.aif");

static void copy(const QString &from, const QString &to) {
 if (!QFile::copy(fixtures + from, to)) qFatal("Could not copy fixture");
}
static void run(const QString &label, const QString &path) {
 MediaScanner scanner;
 MediaScanner::Options options;
 options.includeOmf = true;
 options.manualPaths = {path};
 QSignalSpy done(&scanner, &MediaScanner::scanFinished);
 scanner.startScan(options);
 if (!done.wait(10000)) qFatal("scan timeout");
 const auto rows = done.takeFirst().at(0).value<QVector<MediaFile>>();
 qInfo().noquote() << label << "row count" << rows.size();
 for (const auto &row : rows)
   qInfo().noquote() << " " << row.fileName << "omf" << row.omfEra << "needsHeader" << row.needsHeaderRead
     << "codec" << row.codec << "rate" << row.sampleRate << "name" << row.clipName;
}
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 QTemporaryDir tmp;
 const QString renamed = tmp.path() + "/RenamedArchive";
 QDir().mkpath(renamed);
 copy("omf/mc2026_audio/msmFMID.pmr", renamed + "/msmFMID.pmr");
 copy("omf/mc2026_audio/msmMMOB.mdb", renamed + "/msmMMOB.mdb");
 copy("omf/mc2026_audio/" + wav, renamed + '/' + wav);
 run("Renamed legacy folder", renamed);
 copy("msmFMID.pmr", renamed + "/amaFMID.pmr");
 run("Identical file + unrelated modern PMR", renamed);
 const QString canonical = tmp.path() + "/OMFI MediaFiles";
 QDir().mkpath(canonical);
 copy("omf/mc2026_audio/" + aif, canonical + "/renamed.aiff");
 const auto parsed = OmfParser::parseHeader(canonical + "/renamed.aiff");
 qInfo() << "Direct same AIFF bytes" << "valid" << parsed.essence.valid << "codec" << parsed.essence.codec;
 run("Canonical OMFI, valid .aiff only", canonical);
 qInfo() << "Extension .aiff admitted" << Conventions::hasOmfEraExtension(QStringLiteral("renamed.aiff"));
}
