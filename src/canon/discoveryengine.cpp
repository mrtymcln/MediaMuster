#include "discoveryengine.h"
#include "avidmedialayout.h"
#include "volumeidentity.h"

#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

namespace Canon
{
	namespace
	{
		void filesystemObservation(MediaFile &file, const SourceSnapshotRef &source,
								   MediaProperty property, const QString &locator, const QVariant &value)
		{
			MetadataObservation observation;
			observation.snapshot = source;
			observation.property = locator;
			observation.value = value;
			observation.readState = value.isValid() ? PropertyReadState::Present : PropertyReadState::NotRead;
			file.evidence.observe(property, std::move(observation));
		}

		QStringList managedRoots(const QString &path)
		{
			if (AvidMediaLayout::isMxfRoot(path) || AvidMediaLayout::isOmfRoot(path))
				return {path};
			if (const auto location = AvidMediaLayout::locateMediaFolder(path))
				return {location->rootPath};
			QStringList roots;
			// Only the direct Avid MediaFiles container or a direct media base.
			QDir base(path);
			for (const auto &entry : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
			{
				if (AvidMediaLayout::isMxfRoot(entry.absoluteFilePath()) || AvidMediaLayout::isOmfRoot(entry.absoluteFilePath()))
					roots.append(entry.absoluteFilePath());
				else if (entry.fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0)
					for (const auto &child : QDir(entry.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
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
		const auto scanFolder = [&](const QString &path)
		{
			const QFileInfo folder(path);
			if (!folder.isReadable())
			{
				issue(DiscoveryIssue::Kind::UnreadableFolder, path, QStringLiteral("Cannot inspect the managed folder"));
				return;
			}
			const auto location = AvidMediaLayout::locateMediaFolder(path);
			if (!location)
				return;
			const bool legacy = location->family == AvidMediaLayout::Family::Omf;
			const QStringList filters = legacy ? QStringList{"*.omf", "*.aif", "*.wav", "*.pmr", "*.mdb"} : QStringList{"*.mxf", "*.pmr", "*.mdb"};
			const auto entries = QDir(path).entryInfoList(filters, QDir::Files | QDir::NoSymLinks);
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
					filesystemObservation(file, snapshot, MediaProperty::Location, QStringLiteral("filesystem path"), file.path);
					filesystemObservation(file, snapshot, MediaProperty::Filename, QStringLiteral("filesystem filename"), entry.fileName());
					filesystemObservation(file, snapshot, MediaProperty::Size, QStringLiteral("filesystem byte size"), file.sizeBytes);
					filesystemObservation(file, snapshot, MediaProperty::Created, QStringLiteral("filesystem birth time"),
										  file.created.isValid() ? QVariant(file.created) : QVariant{});
					filesystemObservation(file, snapshot, MediaProperty::Modified, QStringLiteral("filesystem modification time"),
										  file.modified.isValid() ? QVariant(file.modified) : QVariant{});
					filesystemObservation(file, snapshot, MediaProperty::VolumeIdentifier, QStringLiteral("native volume identity"),
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
			const auto roots = managedRoots(info.absoluteFilePath());
			if (roots.isEmpty())
			{
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
				const QString physicalRoot = QFileInfo(root).canonicalFilePath();
				if (visitedRoots.contains(physicalRoot))
					continue;
				visitedRoots.insert(physicalRoot);
				if (!QFileInfo(root).isReadable())
				{
					issue(DiscoveryIssue::Kind::UnreadableFolder, root, QStringLiteral("Cannot enumerate the managed media root"));
					continue;
				}
				if (legacy)
					scanFolder(root);
				for (const auto &child : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
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
