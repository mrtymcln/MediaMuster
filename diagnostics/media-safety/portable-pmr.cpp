#include "mediafile.h"
#include "mediascanner.h"
#include "pmrparser.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>

#include <cstdio>
#include <cstdlib>

// Diagnostic assertions deliberately pin the CURRENT reported behaviour,
// including the bug. A fixed parser should fail the bug assertions here;
// production regression tests should instead assert the desired behaviour.
namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open disposable fixture for writing");
    require(file.write(bytes) == bytes.size(), "write complete disposable fixture");
    require(file.flush(), "flush disposable fixture");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "open repository fixture");
    QByteArray bytes = file.readAll();
    require(file.error() == QFileDevice::NoError, "read complete repository fixture");
    return bytes;
}

void checkParse(const char *label, const QByteArray &bytes, bool expectedOk, qsizetype expectedCount)
{
    QTemporaryDir temp;
    require(temp.isValid(), "create parser temporary directory");
    const QString path = temp.filePath(QStringLiteral("msmFMID.pmr"));
    writeFile(path, bytes);
    bool ok = !expectedOk;
    const auto rows = PmrParser::parse(path, &ok);
    std::printf("PARSE %s: ok=%d entries=%lld\n", label, int(ok), static_cast<long long>(rows.size()));
    require(ok == expectedOk, "parser success matches expected current behaviour");
    require(rows.size() == expectedCount, "parser entry count matches expected current behaviour");
}

void checkScan(const QString &fixtures, const char *label, const QByteArray &bytes,
               bool corruptMdb, MediaFile::DbStatus expectedTone, MediaFile::DbStatus expectedStray)
{
    QTemporaryDir temp;
    require(temp.isValid(), "create scanner temporary directory");
    const QString folder = temp.filePath(QStringLiteral("Avid MediaFiles/MXF/1"));
    require(QDir().mkpath(folder), "create disposable Avid media directory");
    const QDir destination(folder);
    writeFile(destination.filePath(QStringLiteral("msmFMID.pmr")), bytes);
    const QString mdbName = QStringLiteral("msmMMOB.mdb");
    if (corruptMdb)
        writeFile(destination.filePath(mdbName), QByteArray("bad MDB"));
    else
        require(QFile::copy(QDir(fixtures).filePath(mdbName), destination.filePath(mdbName)), "copy repository MDB fixture");
    const QString toneName = QStringLiteral("TONE_100A01.EA7D504A.611740.mxf");
    const QString strayName = QStringLiteral("never-indexed.wav");
    require(QFile::copy(QDir(fixtures).filePath(toneName), destination.filePath(toneName)), "copy repository MXF fixture");
    writeFile(destination.filePath(strayName), QByteArray("RIFF----WAVEfmt "));

    MediaScanner scanner;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    bool finished = false;
    QObject::connect(&scanner, &MediaScanner::scanFinished, &loop, [&](const QVector<MediaFile> &rows) {
        require(rows.size() == 2, "scanner returns exactly the two disposable media files");
        bool toneSeen = false;
        bool straySeen = false;
        for (const auto &row : rows) {
            if (row.fileName == toneName) {
                require(!toneSeen, "tone appears once");
                toneSeen = true;
                require(row.dbStatus == expectedTone, "tone database status matches expected current behaviour");
                require(row.project == QStringLiteral("block 1729"), "tone project recovered from its real metadata");
            } else if (row.fileName == strayName) {
                require(!straySeen, "stray appears once");
                straySeen = true;
                require(row.dbStatus == expectedStray, "stray database status matches expected current behaviour");
            } else {
                require(false, "scanner returned an unexpected media file");
            }
        }
        require(toneSeen && straySeen, "scanner returned tone and stray");
        std::printf("SCAN %s: tone=%s[%d] stray=%s[%d]; project recovered\n", label,
                    qPrintable(MediaFile::dbStatusText(expectedTone).label), int(expectedTone),
                    qPrintable(MediaFile::dbStatusText(expectedStray).label), int(expectedStray));
        finished = true;
        loop.quit();
    });
    MediaScanner::Options options;
    options.volumePaths = QStringList{temp.path()};
    timeout.start(30000);
    scanner.startScan(options);
    loop.exec();
    require(finished, "scanner completed within 30 seconds");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString fixtures;
    if (argc == 2)
        fixtures = QString::fromLocal8Bit(argv[1]);
#ifdef FIXTURES_DIR
    else if (argc == 1)
        fixtures = QStringLiteral(FIXTURES_DIR);
#endif
    require(!fixtures.isEmpty(), "usage: portable-pmr-probe <repository-fixtures-directory>");
    const QByteArray original = readFile(QDir(fixtures).filePath(QStringLiteral("msmFMID.pmr")));
    require(original.size() >= 12, "fixture has a complete PMR header");
    const bool bigEndian = qFromBigEndian<quint32>(original.constData()) == 0x7a9;
    require(bigEndian || qFromLittleEndian<quint32>(original.constData()) == 0x7a9, "fixture has PMR magic");
    QByteArray countZero = original;
    if (bigEndian)
        qToBigEndian(quint32(0), countZero.data() + 8);
    else
        qToLittleEndian(quint32(0), countZero.data() + 8);
    QByteArray unknownHeader(8, '\0');
    if (bigEndian)
        qToBigEndian(qint32(17), unknownHeader.data());
    else
        qToLittleEndian(qint32(17), unknownHeader.data());
    const QByteArray truncated = original.chopped(10);

    checkParse("original", original, true, 1);
    checkParse("base-count-zero-records-retained", countZero, true, 0);
    checkParse("appended-version-17", original + unknownHeader, true, 1);
    checkParse("appended-eight-zeros", original + QByteArray(8, '\0'), true, 1);
    checkParse("truncated-ten-bytes", truncated, false, 1);

    using Status = MediaFile::DbStatus;
    checkScan(fixtures, "original", original, false, Status::Listed, Status::NoReference);
    checkScan(fixtures, "base-count-zero-records-retained", countZero, false, Status::NoReference, Status::NoReference);
    checkScan(fixtures, "truncated-Unicode-section", truncated, false, Status::DbUnreadable, Status::DbUnreadable);
    checkScan(fixtures, "base-count-zero-and-corrupt-MDB", countZero, true, Status::DbUnreadable, Status::DbUnreadable);
    std::puts("PASS: all current-behaviour PMR and scanner assertions matched; false No Reference classification reproduced.");
    return 0;
}
