#pragma once
#include "canon/scanmodel.h"
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
// Diagnostic only: count the existing ScanEngine application-level calls.
// No extra filesystem requests are made inside the scan and no mock is used.
namespace FolderCacheProbe {
inline QMap<QString, qint64> canonicalByFolder, timestampsByFolder, freshChecksByPath;
inline QMap<int, qint64> freshChecksByHint;
inline QString canonical(const QFileInfo &info) {
    ++canonicalByFolder[info.filePath()]; return info.canonicalFilePath();
}
inline QDateTime folderModified(const QString &folder) {
    ++timestampsByFolder[folder]; return QFileInfo(folder).lastModified();
}
inline void freshCheck(const QString &path, Canon::SourceCandidate::ReaderHint hint) {
    ++freshChecksByPath[path]; ++freshChecksByHint[int(hint)];
}
template <typename Key> inline QJsonObject counts(const QMap<Key, qint64> &values) {
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if constexpr (std::is_same_v<Key, QString>) result.insert(it.key(), it.value());
        else result.insert(QString::number(it.key()), it.value());
    }
    return result;
}
template <typename Key> inline qint64 total(const QMap<Key, qint64> &values) {
    qint64 result = 0; for (const auto count : values) result += count; return result;
}
inline QJsonObject json() {
    return {{"canonicalFolderLookups", total(canonicalByFolder)},
            {"folderTimestampQueries", total(timestampsByFolder)},
            {"freshCheckUnchangedProbes", total(freshChecksByPath)},
            {"canonicalByFolder", counts(canonicalByFolder)},
            {"timestampsByFolder", counts(timestampsByFolder)},
            {"freshChecksByPath", counts(freshChecksByPath)},
            {"freshChecksByReaderHint", counts(freshChecksByHint)}};
}
}
