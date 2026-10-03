// Walks the agreed Avid folders, and records what is physically present.
// Copies in different locations keep separate rows. Folder access failures stay
// visible so later matching can account for incomplete parts of the scan.

#include "discoveryengine.h"
#include "discoveryengine_p.h"
#include "avidmedialayout.h"
#include "volumeidentity.h"

#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <algorithm>
#include <filesystem>

namespace Canon
{
	namespace
	{
		struct DirectoryListing
		{
			QFileInfoList entries;
			QString error; // First failure; keep any entries successfully inspected.
		};

		// QDir::entryInfoList cannot distinguish an empty folder from a failed read.
		// Use checked enumeration, retaining Qt's path conversion and admission rules.
		DirectoryListing listEntries(const QString &path, QDir::Filter type,
									 const QStringList &filters, const Cancellation &cancellation)
		{
			DirectoryListing result;
			if (cancellation.cancelled())
				return result;
			std::error_code error;
			std::filesystem::directory_iterator current(QDir(path).filesystemPath(), error), end;
			while (!error && current != end && !cancellation.cancelled())
			{
				const QFileInfo entry(current->path());
				if (!Conventions::isDotHidden(entry.fileName()) && !entry.isHidden() &&
					(filters.isEmpty() || QDir::match(filters, entry.fileName())))
				{
					std::error_code statusError;
					// The free function checks access; directory_entry may cache a type
					// from the listing even when the entry itself cannot be inspected.
					const auto status = std::filesystem::symlink_status(current->path(), statusError);
					if (statusError)
					{
						if (result.error.isEmpty())
							result.error = QStringLiteral("Cannot inspect %1: %2")
											   .arg(entry.absoluteFilePath(), QString::fromLocal8Bit(statusError.message().c_str()));
					}
					else if (!entry.isSymLink() && !std::filesystem::is_symlink(status) &&
							 (type == QDir::Dirs ? std::filesystem::is_directory(status) : std::filesystem::is_regular_file(status)))
						result.entries.append(entry);
				}
				current.increment(error);
			}
			if (error && result.error.isEmpty())
				result.error = QStringLiteral("Cannot enumerate directory: %1").arg(QString::fromLocal8Bit(error.message().c_str()));
			std::sort(result.entries.begin(), result.entries.end(), [](const QFileInfo &a, const QFileInfo &b)
					  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
			return result;
		}

		template <typename ListDirectories>
		QStringList managedRoots(const QString &path, const ListDirectories &listDirectories)
		{
			if (AvidMediaLayout::isMxfRoot(path) || AvidMediaLayout::isOmfRoot(path))
				return {path};
			if (const auto location = AvidMediaLayout::locateMediaFolder(path))
				return {location->rootPath};
			QStringList roots;
			// Only the direct Avid MediaFiles container or a direct media base.
			for (const auto &entry : listDirectories(path))
			{
				if (AvidMediaLayout::isMxfRoot(entry.absoluteFilePath()) || AvidMediaLayout::isOmfRoot(entry.absoluteFilePath()))
					roots.append(entry.absoluteFilePath());
				else if (entry.fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0)
					for (const auto &child : listDirectories(entry.absoluteFilePath()))
						if (AvidMediaLayout::isMxfRoot(child.absoluteFilePath()))
							roots.append(child.absoluteFilePath());
			}
			return roots;
		}
	}

	ScanResult DiscoveryEngine::discover(const ScanRequest &request, const Cancellation &cancellation) const
	{
		ScanResult result;
		result.request = request;
		KelpieIdAllocator ids;
		QSet<QString> visitedRoots;
		QHash<QString, QString> volumeIdentifiers;
		const auto issue = [&](DiscoveryIssue::Kind kind, const QString &path, const QString &explanation)
		{
			result.discoveryComplete = false;
			result.discoveryIssues.append({kind, path, explanation});
		};
		const auto list = [&](const QString &path, QDir::Filter type, const QStringList &filters = QStringList{})
		{
			auto listing = listEntries(path, type, filters, cancellation);
			if (!listing.error.isEmpty())
				issue(DiscoveryIssue::Kind::UnreadableFolder, path, listing.error);
			return std::move(listing.entries);
		};
		const auto scanFolder = [&](const QString &path)
		{
			const auto location = AvidMediaLayout::locateMediaFolder(path);
			if (!location)
				return;
			const bool legacy = location->family == AvidMediaLayout::Family::Omf;
			const QStringList filters = legacy ? QStringList{"*.omf", "*.aif", "*.wav", "*.pmr", "*.mdb"} : QStringList{"*.mxf", "*.pmr", "*.mdb"};
			const auto entries = list(path, QDir::Files, filters);
			const QString volumeRoot = QStorageInfo(path).rootPath();
			if (!volumeIdentifiers.contains(volumeRoot))
				volumeIdentifiers.insert(volumeRoot, VolumeIdentity::capture(path).identifier());
			for (const auto &entry : entries)
			{
				if (cancellation.cancelled())
					return;
				const QString suffix = entry.suffix().toLower();
				const auto hint = suffix == QStringLiteral("pmr") ? SourceCandidate::ReaderHint::Pmr : suffix == QStringLiteral("mdb") ? SourceCandidate::ReaderHint::Mdb
																								   : legacy							   ? SourceCandidate::ReaderHint::LegacyMedia
																																	   : SourceCandidate::ReaderHint::Mxf;
				SourceCandidate candidate{hint, entry.absoluteFilePath(), entry.lastModified()};
				if (hint == SourceCandidate::ReaderHint::Mxf || hint == SourceCandidate::ReaderHint::LegacyMedia)
				{
					MediaFile file;
					file.kelpieId = ids.allocate();
					file.path = candidate.path;
					file.volumeIdentifier = volumeIdentifiers.value(volumeRoot);
					file.sizeBytes = entry.size();
					file.created = entry.birthTime();
					file.modified = candidate.modified;
					file.omfScan = legacy;
					file.quarantined = location->isQuarantined;
					file.stamp = {file.path, file.volumeIdentifier, file.modified, {}, {}};
					const SourceSnapshotRef snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
						MetadataSource::Filesystem, file.path, file.modified, SourceReadState::Complete});
					Detail::filesystemObservation(file, snapshot, MediaProperty::Location, QStringLiteral("filesystem path"), file.path);
					Detail::filesystemObservation(file, snapshot, MediaProperty::Filename, QStringLiteral("filesystem filename"), entry.fileName());
					Detail::filesystemObservation(file, snapshot, MediaProperty::Size, QStringLiteral("filesystem byte size"), file.sizeBytes);
					Detail::filesystemObservation(file, snapshot, MediaProperty::Created, QStringLiteral("filesystem birth time"),
												  file.created.isValid() ? QVariant(file.created) : QVariant{});
					Detail::filesystemObservation(file, snapshot, MediaProperty::Modified, QStringLiteral("filesystem modification time"),
												  file.modified.isValid() ? QVariant(file.modified) : QVariant{});
					Detail::filesystemObservation(file, snapshot, MediaProperty::VolumeIdentifier, QStringLiteral("native volume identity"),
												  file.volumeIdentifier.isEmpty() ? QVariant{} : QVariant(file.volumeIdentifier));
					candidate.kelpieId = file.kelpieId;
					result.files.append(std::move(file));
				}
				result.candidates.append(std::move(candidate));
			}
		};

		for (const auto &requested : request.roots)
		{
			if (cancellation.cancelled())
				break;
			const QFileInfo info(requested);
			if (!info.isDir() || !info.isReadable() || info.isSymLink())
			{
				issue(DiscoveryIssue::Kind::UnavailableRoot, requested, QStringLiteral("Requested directory is unavailable, unreadable or a symlink"));
				continue;
			}
			const auto issuesBeforeListing = result.discoveryIssues.size();
			const auto roots = managedRoots(info.absoluteFilePath(), [&](const QString &path)
											{ return list(path, QDir::Dirs); });
			if (roots.isEmpty())
			{
				if (!cancellation.cancelled() && result.discoveryIssues.size() == issuesBeforeListing)
					issue(DiscoveryIssue::Kind::UnmanagedRoot, requested, QStringLiteral("No managed media root at the requested location or its immediate base"));
				continue;
			}
			for (const auto &root : roots)
			{
				if (cancellation.cancelled())
					break;
				const bool legacy = AvidMediaLayout::isOmfRoot(root);
				if (legacy && !request.omfScan)
					continue;
				const QFileInfo rootInfo(root);
				const QString canonicalRoot = rootInfo.canonicalFilePath();
				const QString physicalRoot = canonicalRoot.isEmpty() ? rootInfo.absoluteFilePath() : canonicalRoot;
				if (visitedRoots.contains(physicalRoot))
					continue;
				visitedRoots.insert(physicalRoot);
				if (legacy)
					scanFolder(root);
				for (const auto &child : list(root, QDir::Dirs))
				{
					if (cancellation.cancelled())
						break;
					if (AvidMediaLayout::locateMediaFolder(child.absoluteFilePath()))
						scanFolder(child.absoluteFilePath());
				}
			}
		}
		result.cancelled = cancellation.cancelled();
		result.discoveryComplete = result.discoveryComplete && !result.cancelled;
		return result;
	}
}
