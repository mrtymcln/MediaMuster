#include "oprunner.h"
#include "operationrecovery.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
struct Sink : OpSink {
    QVector<OpResult> results;
    void progress(const QString &, int, int, double) override {}
    void log(QtMsgType, const QString &message) override { std::fprintf(stderr, "%s\n", qPrintable(message)); }
    void trashUsed(const QString &, int) override {}
    void result(const OpResult &r) override { results.append(r); }
};
static QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
static int dirs(const QString &path) { return QDir(path).entryList({".mediamuster-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).size(); }
static void run(const char *label, bool move, const char *cancelAt = nullptr, bool retryErrors = false) {
    QTemporaryDir temp;
    if (!temp.isValid()) std::abort();
    const auto root = OpJournal::canonicalPath(temp.path());
    const auto sourceDir = root + "/source", dest = root + "/destination", journals = root + "/journals", src = sourceDir + "/clip.bin";
    if (!QDir().mkpath(sourceDir) || !QDir().mkpath(dest)) std::abort();
    const QByteArray bytes(8 * 1024 * 1024, 'm');
    QFile f(src); if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) std::abort(); f.close();
    OpRequest req; req.kind = move ? OpKind::Move : OpKind::Copy; req.verifyCopies = true; req.destRoot = dest;
    req.diagnosticTrashRoot = root + "/trash";
    OpItem i; i.src = src; i.name = "clip.bin"; i.bytes = bytes.size(); req.items.append(i);
    Sink sink; std::atomic<bool> cancel{false}; OpRunner runner(sink, cancel); runner.hooks.forceCopy = true;
    bool checkpointReached = false;
    if (cancelAt) runner.hooks.checkpoint = [&](const QString &stage, const auto &) { if (stage == cancelAt) { checkpointReached = true; cancel = true; } };
    if (retryErrors) runner.hooks.nativeCopyError = [](const auto &) {
#ifdef Q_OS_WIN
        return int(ERROR_NETWORK_BUSY);
#else
        return EBUSY;
#endif
    };
    auto totals = runner.run(req, journals);
    auto records = OpJournal::scan(journals);
    if (records.size() != 1 || records[0].entries.size() != 1 || sink.results.isEmpty()) std::abort();
    const auto entry = records[0].entries[0];
    qint64 artifactBytes = 0; int artifacts = 0;
    for (const auto &p : entry.artifacts) if (QFileInfo::exists(p)) { ++artifacts; artifactBytes += QFileInfo(p).size(); }
    std::printf("%s: succeeded=%d cancelled=%d sourceExists=%d destinationIntact=%d sourceHiddenDirs=%d destinationHiddenDirs=%d artifacts=%d artifactBytes=%lld retirementIntact=%d sourceRemovedFlag=%d attempts=%d\n",
        label, totals.succeeded, totals.cancelled, QFileInfo::exists(src), read(dest + "/clip.bin") == bytes, dirs(sourceDir), dirs(dest), artifacts, (long long)artifactBytes, !entry.retirement.isEmpty() && read(entry.retirement) == bytes, sink.results.last().sourceRemoved, entry.attempts);
    std::printf("details: failed=%d needsAttention=%d sourceIntact=%d checkpointReached=%d resultState=%d message=%s\n", totals.failed, totals.needsAttention, read(src) == bytes, checkpointReached, int(sink.results.last().state), qPrintable(sink.results.last().message));
    std::fflush(stdout);
    if ((!cancelAt && !retryErrors && totals.succeeded != 1) || (cancelAt && !checkpointReached)) std::abort();
    if (cancelAt && QByteArray(cancelAt) == "source-retired") {
        QString err;
        bool dismissed = OpJournal::dismiss(records[0].path, err);
        auto history = OperationRecovery::run(journals);
        std::printf("after dismiss/recovery: dismissed=%d sourceExists=%d retirementIntact=%d pendingJobs=%lld\n", dismissed, QFileInfo::exists(src), read(entry.retirement) == bytes, (long long)history.resumable.size());
    }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    run("successful copy", false);
    run("cancelled copy", false, "copy-chunk");
    run("successful copy-based move", true);
    run("cancelled after retirement", true, "source-retired");
    run("three failed attempts", false, nullptr, true);
}
