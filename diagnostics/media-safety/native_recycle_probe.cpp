#include "oprunner.h"
#include "optrash.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStorageInfo>
#include <QSysInfo>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shlguid.h>
#endif

namespace {
QJsonObject evidence;
QString output;
void persist()
{
    QSaveFile file(output);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(evidence).toJson(QJsonDocument::Indented));
        file.commit();
    }
}
void stage(const QString &name)
{
    evidence["stage"] = name;
    persist();
    const auto bytes = name.toUtf8();
    std::fprintf(stdout, "%s\n", bytes.constData());
    std::fflush(stdout);
}
QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
QJsonArray matchingFiles(const QString &scanRoot, qint64 size, const QString &hash)
{
    QJsonArray found;
    QJsonArray unreadable;
    QDirIterator it(scanRoot, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext() && count++ < 10000) {
        const auto path = it.next();
#ifdef Q_OS_WIN
        if (it.fileInfo().isDir() && !it.fileInfo().isSymLink()) {
            const auto pattern=QDir::toNativeSeparators(path+"/*");
            WIN32_FIND_DATAW data{};
            const auto handle=::FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()),&data);
            if(handle!=INVALID_HANDLE_VALUE) ::FindClose(handle);
            else {
                const auto error=::GetLastError();
                if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_NO_MORE_FILES)
                    unreadable.append(QJsonObject{{"path",path},{"win32Error",int(error)}});
            }
            continue;
        }
#endif
        if (!it.fileInfo().isSymLink() && it.fileInfo().size() == size && hashFile(path) == hash)
            found.append(path);
    }
    evidence["scanLimitReached"] = it.hasNext();
    evidence["unreadableScanDirectories"] = unreadable;
    return found;
}
QJsonObject totals(const OpRunner::Totals &t)
{
    return {{"succeeded",t.succeeded},{"unchanged",t.unchanged},{"failed",t.failed},
        {"skipped",t.skipped},{"retained",t.retained},{"needsAttention",t.needsAttention},
        {"cancelled",t.cancelled}};
}
struct Sink : OpSink {
    void progress(const QString &, int, int, double) override {}
    void log(QtMsgType type, const QString &message) override {
        auto logs = evidence["logs"].toArray();
        logs.append(QJsonObject{{"type",int(type)},{"message",message}});
        evidence["logs"] = logs;
        persist();
    }
    void trashUsed(const QString &path, int count) override {
        evidence["trashUsed"] = QJsonObject{{"path",path},{"count",count}};
        persist();
    }
    void result(const OpResult &r) override {
        auto results = evidence["results"].toArray();
        results.append(QJsonObject{{"state",int(r.state)},{"source",r.source},
            {"destination",r.destination},{"message",r.message},{"sourceRemoved",r.sourceRemoved}});
        evidence["results"] = results;
        persist();
    }
};

#ifdef Q_OS_WIN
template<class T> struct ComPtr {
    T *value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
    T **put() { return &value; }
    T *operator->() const { return value; }
};
QString hrText(HRESULT status) {
    return QStringLiteral("0x%1").arg(quint32(status), 8, 16, QLatin1Char('0'));
}
QString shellName(IShellItem *item, SIGDN kind) {
    PWSTR name = nullptr;
    if (!item || FAILED(item->GetDisplayName(kind, &name))) return {};
    const auto result = QDir::fromNativeSeparators(QString::fromWCharArray(name));
    ::CoTaskMemFree(name);
    return result;
}
class RecycleAdvice final : public ITransferAdviseSink {
    LONG references = 1;
    void record(const QString &method, HRESULT status = S_OK) {
        auto callbacks = evidence["nativeCallbacks"].toArray();
        callbacks.append(QJsonObject{{"method",method},{"status",hrText(status)}});
        evidence["nativeCallbacks"] = callbacks;
        persist();
    }
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_ITransferAdviseSink) return E_NOINTERFACE;
        *out = static_cast<ITransferAdviseSink *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(::InterlockedIncrement(&references)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = ::InterlockedDecrement(&references);
        if (!count) delete this;
        return ULONG(count);
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(ULONGLONG, ULONGLONG, int, int, int, int) override {
        record("UpdateProgress");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateTransferState(TRANSFER_ADVISE_STATE state) override {
        record("UpdateTransferState", HRESULT(state));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConfirmOverwrite(IShellItem *, IShellItem *, LPCWSTR) override {
        record("ConfirmOverwrite", E_ABORT);
        return E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE ConfirmEncryptionLoss(IShellItem *) override {
        record("ConfirmEncryptionLoss", E_ABORT);
        return E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE FileFailure(IShellItem *, LPCWSTR, HRESULT error, LPWSTR, ULONG) override {
        record("FileFailure", error);
        return FAILED(error) ? error : E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE SubStreamFailure(IShellItem *, LPCWSTR, HRESULT error) override {
        record("SubStreamFailure", error);
        return FAILED(error) ? error : E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE PropertyFailure(IShellItem *, const PROPERTYKEY *, HRESULT error) override {
        record("PropertyFailure", error);
        return FAILED(error) ? error : E_ABORT;
    }
};
OpTrash::Result recycleDirect(const QString &source, const OpStamp &expected) {
    evidence["nativeApi"] = "ITransferSource::RecycleItem";
    evidence["nativeFlags"] = int(TSF_NORMAL);
    if (OpTrash::isNetwork(source))
        return {OpTrash::Outcome::Unavailable, {}, {}, "Network bypass; no native operation attempted.", {}};
    struct Apartment {
        HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) ::CoUninitialize(); }
    } apartment;
    const auto failure = [&](const QString &step, HRESULT status) -> OpTrash::Result {
        evidence["nativeFailureStep"] = step;
        evidence["nativeFailureStatus"] = hrText(status);
        persist();
        const auto outcome = expected.unchanged(OpFile::inspect(source)) ?
            OpTrash::Outcome::Unavailable : OpTrash::Outcome::Failed;
        return {outcome, {}, {}, step + " failed: " + hrText(status), {}};
    };
    if (FAILED(apartment.result)) return failure("CoInitializeEx", apartment.result);
    ComPtr<IShellItem> item, parent, recycleBin, created;
    const auto native = QDir::toNativeSeparators(source);
    HRESULT status = ::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()),
                                                  nullptr, IID_PPV_ARGS(item.put()));
    if (FAILED(status)) return failure("SHCreateItemFromParsingName", status);
    status = item->GetParent(parent.put());
    if (FAILED(status)) return failure("GetParent", status);
    ComPtr<ITransferSource> transfer;
    status = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(transfer.put()));
    if (FAILED(status)) return failure("BindToHandler(BHID_Transfer)", status);
    ComPtr<RecycleAdvice> advice;
    advice.value = new RecycleAdvice;
    DWORD cookie = 0;
    status = transfer->Advise(advice.value, &cookie);
    if (FAILED(status)) return failure("Advise", status);
    struct Unadvise {
        ITransferSource *transfer;
        DWORD cookie;
        ~Unadvise() { transfer->Unadvise(cookie); }
    } unadvise{transfer.value, cookie};
    PIDLIST_ABSOLUTE binId = nullptr;
    status = ::SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, &binId);
    if (FAILED(status)) return failure("SHGetKnownFolderIDList", status);
    status = ::SHCreateItemFromIDList(binId, IID_PPV_ARGS(recycleBin.put()));
    ::CoTaskMemFree(binId);
    if (FAILED(status)) return failure("SHCreateItemFromIDList(recycle bin)", status);
    if (!expected.unchanged(OpFile::inspect(source))) return failure("Source revalidation", E_ABORT);
    stage("native-recycle-entered");
    status = transfer->RecycleItem(item.value, recycleBin.value, TSF_NORMAL, created.put());
    evidence["nativeRecycleStatus"] = hrText(status);
    evidence["nativeReturnedItem"] = created.value != nullptr;
    const auto landedPath = shellName(created.value, SIGDN_FILESYSPATH);
    evidence["nativeReturnedPath"] = landedPath;
    evidence["nativeReturnedParsingName"] = shellName(created.value, SIGDN_DESKTOPABSOLUTEPARSING);
    const auto landed = OpFile::inspect(landedPath);
    evidence["nativeReturnedStamp"] = landed.json();
    evidence["nativeReturnedHash"] = hashFile(landedPath);
    stage("native-recycle-returned");
    if (FAILED(status)) return failure("RecycleItem", status);
    if (!created.value || !expected.unchanged(landed) || OpFile::occupied(source))
        return failure("Recycle result identity/location validation", E_FAIL);
    PIDLIST_ABSOLUTE landedId = nullptr;
    status = ::SHGetIDListFromObject(created.value, &landedId);
    if (FAILED(status) || !landedId) return failure("SHGetIDListFromObject", status);
    const QByteArray identifier(reinterpret_cast<const char *>(landedId), int(::ILGetSize(landedId)));
    ::CoTaskMemFree(landedId);
    const auto receipt = OpTrashPlatform::receipt("windows", landedPath, landed, identifier);
    return {OpTrash::Outcome::Succeeded, landedPath, receipt, {}, landed};
}
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("MediaMuster disposable Trash diagnostic");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"case","Case name","name"},{"root","New disposable case directory","path"},
        {"output","Evidence JSON file","path"},{"bytes","Generated fixture size","count","65536"},
        {"scan-volume","Scan only the volume labeled MMTRASH_DIAG for the generated fixture"},
        {"mode","production or control","mode","production"}});
    parser.process(app);
    const auto root = QDir::cleanPath(QDir::fromNativeSeparators(parser.value("root")));
    const auto caseName = parser.value("case");
    const auto mode = parser.value("mode");
    output = parser.value("output");
    bool sizeOk = false;
    const auto size = parser.value("bytes").toLongLong(&sizeOk);
    if (root.isEmpty() || !root.contains("_mediamuster-trash-diagnostic/") || output.isEmpty() ||
        caseName.isEmpty() || !sizeOk || size < 1 || size > 16*1024*1024 ||
        (mode != "production" && mode != "control")) return 64;
    evidence = {{"case",caseName},{"mode",mode},{"root",root},{"bytes",size},
        {"os",QSysInfo::prettyProductName()},{"qt",qVersion()}};
    QString scanRoot = root;
#ifdef Q_OS_WIN
    const auto nativeRoot = QDir::toNativeSeparators(root);
    wchar_t volumeRoot[32768]{}, volumeGuid[128]{}, volumeLabel[256]{};
    // Parent exists on the target volume, but case directory must not contain an existing fixture.
    if (!QDir().mkpath(root) || !::GetVolumePathNameW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16()),volumeRoot,32768)) return 65;
    ::GetVolumeNameForVolumeMountPointW(volumeRoot,volumeGuid,128);
    ::GetVolumeInformationW(volumeRoot,volumeLabel,256,nullptr,nullptr,nullptr,nullptr,0);
    evidence["volumeRoot"] = QString::fromWCharArray(volumeRoot);
    evidence["volumeGuid"] = QString::fromWCharArray(volumeGuid);
    evidence["volumeLabel"] = QString::fromWCharArray(volumeLabel);
    evidence["driveType"] = int(::GetDriveTypeW(volumeRoot));
    if (parser.isSet("scan-volume")) {
        if (QString::fromWCharArray(volumeLabel) != "MMTRASH_DIAG") return 66;
        scanRoot = QDir::fromNativeSeparators(QString::fromWCharArray(volumeRoot));
    }
    if (mode == "control" && !parser.isSet("scan-volume")) return 67;
#else
    if (!QDir().mkpath(root) || mode == "control") return 68;
#endif
    const auto source = root + "/" + mode + ".bin";
    const auto journals = root + "/journals";
    const QByteArray seed = QCryptographicHash::hash((caseName + ":" + mode).toUtf8(),QCryptographicHash::Sha256);
    QByteArray block(65536,'\0');
    for (qsizetype i=0;i<block.size();++i) block[i] = seed[i % seed.size()];
    QFile fixture(source);
    if (!fixture.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 69;
    for(qint64 done=0;done<size;) {
        const auto length=qMin(qint64(block.size()),size-done);
        if(fixture.write(block.constData(),length)!=length) return 70;
        done+=length;
    }
    fixture.close();
    const auto hash=hashFile(source);
    if(hash.isEmpty()) {
        evidence["error"]="Cannot hash the newly generated fixture; no deletion was attempted.";
        stage("fixture-hash-failed");
        return 71;
    }
    evidence["source"] = source;
    evidence["expectedSha256"] = hash;
    evidence["beforeStamp"] = OpFile::inspect(source).json();
    evidence["networkClassified"] = OpTrash::isNetwork(source);
    stage("fixture-created");
    if (mode == "control") {
#ifdef Q_OS_WIN
        // Deliberately expendable control only: proves whether the configured legacy
        // recycle policy is effective. Production flags/guard are not changed.
        QString nativeSource = QDir::toNativeSeparators(source);
        nativeSource.append(QChar('\0'));
        nativeSource.append(QChar('\0'));
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = reinterpret_cast<LPCWSTR>(nativeSource.utf16());
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        stage("legacy-control-entered");
        const int result = ::SHFileOperationW(&operation);
        evidence["legacyShellResult"] = result;
        evidence["legacyShellAborted"] = bool(operation.fAnyOperationsAborted);
        evidence["sourceExists"] = QFileInfo::exists(source);
        auto matches = matchingFiles(scanRoot,size,hash);
        evidence["matchingContentPaths"] = matches;
        evidence["classification"] = QFileInfo::exists(source) ? "control-original-retained" :
            (matches.isEmpty() ? "control-original-gone-no-matching-content-found" : "control-recycled");
        stage("complete");
        return 0;
#endif
    }
#ifdef Q_OS_WIN
    std::atomic<bool> cancel{false};
    stage("production-delete-entered");
    const auto direct = recycleDirect(source, OpFile::inspect(source));
    evidence["nativeOutcome"] = int(direct.outcome);
    evidence["nativeError"] = direct.error;
    evidence["nativeReceipt"] = direct.receipt;
    evidence["forwardTotals"] = QJsonObject{{"succeeded",direct.outcome==OpTrash::Outcome::Succeeded ? 1 : 0},
                                           {"failed",direct.outcome==OpTrash::Outcome::Succeeded ? 0 : 1}};
    evidence["sourceExistsAfterDelete"] = QFileInfo::exists(source);
    evidence["sourceHashAfterDelete"] = hashFile(source);
    evidence["matchingPathsAfterDelete"] = matchingFiles(scanRoot,size,hash);
    stage("production-delete-returned");
    if (direct.outcome == OpTrash::Outcome::Succeeded) {
        stage("production-undo-entered");
        const auto undo = OpTrash::restore(direct.receipt,source,direct.landed,cancel);
        evidence["undoOutcome"] = int(undo.outcome);
        evidence["undoError"] = undo.error;
        evidence["undoTotals"] = QJsonObject{{"succeeded",undo.outcome==OpTrash::Outcome::Succeeded ? 1 : 0},
                                            {"failed",undo.outcome==OpTrash::Outcome::Succeeded ? 0 : 1}};
    }
#else
    Sink sink;
    std::atomic<bool> cancel{false};
    OpRunner runner(sink,cancel);
    runner.hooks.checkpoint = [&](const QString &name,const OpJournal::Entry &entry) {
        evidence["latestCheckpoint"] = name;
        evidence["latestCheckpointEntry"] = entry.json();
        persist();
    };
    OpRequest request;
    request.kind = OpKind::Delete;
    OpItem item;
    item.src = source;
    item.name = QFileInfo(source).fileName();
    item.bytes = size;
    request.items.append(item);
    stage("production-delete-entered");
    const auto forward = runner.run(request,journals);
    evidence["forwardTotals"] = totals(forward);
    evidence["sourceExistsAfterDelete"] = QFileInfo::exists(source);
    evidence["sourceHashAfterDelete"] = hashFile(source);
    auto matches = matchingFiles(scanRoot,size,hash);
    evidence["matchingPathsAfterDelete"] = matches;
    QString undoJournal;
    QJsonArray records;
    for(const auto &record:OpJournal::scan(journals)) {
        QJsonArray entries;
        for(const auto &entry:record.entries) {
            auto row=entry.json();
            row["destinationSha256"] = hashFile(entry.dst);
            entries.append(row);
        }
        records.append(QJsonObject{{"path",record.path},{"entries",entries}});
        if(record.request.kind==OpKind::Delete) undoJournal=record.path;
    }
    evidence["forwardJournals"] = records;
    stage("production-delete-returned");
    if (!QFileInfo::exists(source) && !undoJournal.isEmpty()) {
        OpRequest undo;
        undo.kind=OpKind::Undo;
        undo.undoEnabled=true;
        undo.undoJournalPath=undoJournal;
        stage("production-undo-entered");
        evidence["undoTotals"] = totals(runner.run(undo,journals));
    }
#endif
    const bool restored=hashFile(source)==hash;
    evidence["sourceExistsAtEnd"] = QFileInfo::exists(source);
    evidence["sourceHashAtEnd"] = hashFile(source);
    const auto finalMatches=matchingFiles(scanRoot,size,hash);
    evidence["matchingPathsAtEnd"] = finalMatches;
    evidence["classification"] = restored ? (evidence["sourceExistsAfterDelete"].toBool() ?
        "preserved-original" : "restored-after-undo") :
        (finalMatches.isEmpty() ? "unresolved-no-matching-content-found" : "matching-content-retained-needs-attention");
    stage("complete");
    return restored || !finalMatches.isEmpty() ? 0 : 2;
}
