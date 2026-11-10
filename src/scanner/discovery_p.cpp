#include "discovery_p.h"

#include "media/avid_media_layout.h"
#include "media/media_evidence.h"
#include "storage/volume_identity.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStorageInfo>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <utility>

namespace Bella::Detail
{
	namespace
	{
		namespace fs = std::filesystem;

		// MARK: - Directory traversal helpers

		struct PreparedRoot
		{
			QString path;
			QString volumePath;
			QString volumeName;
		};

		QString absoluteFolder(const QString &path)
		{
			const QFileInfo info(path);
			const QString canonical = info.canonicalFilePath();
			return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
		}

		bool isAvidTree(const QString &path)
		{
			return QFileInfo(path).fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
		}

		QString errorText(const std::error_code &error)
		{
			return QString::fromLocal8Bit(error.message().c_str());
		}

		bool nameBefore(const QString &a, const QString &b)
		{
			return a.compare(b, Qt::CaseInsensitive) < 0;
		}

		// A visitor may finish early when it finds a requested root name. Only a
		// clean end of iteration can establish a completed media-folder listing.
		template <typename Visit>
		bool walkDirectory(const QString &folder, ScanControl &control, QString &problem, Visit visit)
		{
			if (control.cancelled())
				return false;
			std::error_code error;
			fs::directory_iterator current(QDir(folder).filesystemPath(), error), end;
			while (!error && current != end)
			{
				if (control.cancelled())
					return false;
				if (!visit(current->path()) || control.cancelled())
					return false;
				current.increment(error);
			}
			if (error && problem.isEmpty())
				problem = QStringLiteral("Cannot enumerate directory: %1").arg(errorText(error));
			return !error;
		}

		std::optional<fs::file_status> visibleStatus(const fs::path &path, const QFileInfo &info,
													 ScanControl &control, QString &problem)
		{
			if (control.cancelled() || info.isHidden())
				return std::nullopt;
			if (control.cancelled())
				return std::nullopt;
			std::error_code error;
			// directory_entry can cache a type even when this entry is inaccessible.
			const auto status = fs::symlink_status(path, error);
			if (error)
			{
				if (problem.isEmpty())
					problem = QStringLiteral("Cannot inspect %1: %2").arg(info.absoluteFilePath(), errorText(error));
				return std::nullopt;
			}
			if (fs::is_symlink(status) || control.cancelled() || info.isSymLink())
				return std::nullopt;
			return status;
		}

		std::optional<qint64> fileSize(const fs::path &path)
		{
			std::error_code error;
			const auto size = fs::file_size(path, error);
			if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<qint64>::max()))
				return std::nullopt;
			return static_cast<qint64>(size);
		}

		// MARK: - Discovery run

		class DiscoveryRun
		{
		public:
			DiscoveryRun(const ScanRequest &request, ScanControl &control, ScanResult &result,
						 DiscoveredDatabases &databases, const ScanCallbacks &callbacks)
				: m_request(request), m_control(control), m_result(result), m_databases(databases), m_callbacks(callbacks)
			{
			}

			void run()
			{
				for (const auto &location : m_request.locations)
				{
					if (m_control.cancelled())
						break;
					prepareLocation(location);
				}
				for (const auto &root : m_roots)
				{
					if (m_control.cancelled())
						break;
					walkRoot(root);
				}
				m_result.discoveryComplete = m_complete && !m_control.cancelled();
			}

		private:
			void report(const QString &path, const QString &reason)
			{
				m_complete = false;
				const ScanProblem problem{path, reason};
				m_result.problems.append(problem);
				if (m_callbacks.warning)
					m_callbacks.warning(problem);
			}

			void discovering(const QString &path)
			{
				if (!m_control.cancelled() && m_callbacks.discovering)
					m_callbacks.discovering(path);
			}

			QString namedChild(const QString &parent, QLatin1String name)
			{
				if (m_control.cancelled())
					return {};
				const QString expected = QDir(parent).filePath(name);
				if (QFileInfo(expected).isDir())
					return expected;
				QString found;
				QString problem;
				walkDirectory(parent, m_control, problem, [&](const fs::path &path)
							  {
					const QFileInfo info(path);
					if (info.fileName().compare(name, Qt::CaseInsensitive) != 0)
						return true;
					std::error_code error;
					// A selected tree alias is resolved before managed-root admission.
					const auto status = fs::status(path, error);
					if (error)
						report(info.absoluteFilePath(), QStringLiteral("Cannot inspect media folders: %1").arg(errorText(error)));
					if (!error && fs::is_directory(status))
					{
						found = info.absoluteFilePath();
						return false;
					}
					return true; });
				if (!problem.isEmpty())
					report(parent, problem);
				return found;
			}

			// MARK: - Root preparation

			bool addRoot(const QString &path, const QString &volumePath, const QString &volumeName)
			{
				if (m_control.cancelled())
					return false;
				const QString physical = absoluteFolder(path);
				if (m_control.cancelled())
					return false;
				if (AvidMediaLayout::isInsideUmeRoot(path) || AvidMediaLayout::isInsideUmeRoot(physical) ||
					(!AvidMediaLayout::isMxfRoot(physical) && !AvidMediaLayout::isOmfRoot(physical)))
				{
					report(path, QStringLiteral("Requested location is not a supported MXF or OMFI media root"));
					return false;
				}
				if (!m_rootPaths.contains(physical))
				{
					m_rootPaths.insert(physical);
					m_roots.append(PreparedRoot{physical, volumePath, volumeName});
				}
				return true;
			}

			void prepareBase(const ScanLocation &location, const QString &path, const QString &label)
			{
				const bool required = location.kind == ScanLocation::Kind::RequiredMediaBase;
				const QString volumePath = required ? path : location.path;
				const auto problemsBefore = m_result.problems.size();
				bool found = false;
				const QString avid = namedChild(path, Conventions::kAvidMediaFilesDir);
				if (!avid.isEmpty())
				{
					const QString mxf = namedChild(avid, Conventions::kMxfDir);
					if (!mxf.isEmpty())
						found = addRoot(mxf, volumePath, label);
				}
				if (m_request.includeOmf || required)
				{
					const QString omf = namedChild(path, Conventions::kOmfMediaFilesDir);
					if (!omf.isEmpty() && !m_control.cancelled())
					{
						const bool legacyFound = m_request.includeOmf ? addRoot(omf, volumePath, label)
																	  : AvidMediaLayout::isOmfRoot(absoluteFolder(omf));
						found = found || legacyFound;
					}
				}
				if (required && !found && !m_control.cancelled() && m_result.problems.size() == problemsBefore)
					report(location.path, QStringLiteral("No managed media trees are present at the requested location"));
			}

			void prepareLocation(const ScanLocation &location)
			{
				if (location.path.isEmpty())
				{
					report({}, QStringLiteral("No directory was supplied for this scan location"));
					return;
				}
				discovering(location.path);
				if (m_control.cancelled())
					return;
				const QString path = absoluteFolder(location.path);
				if (m_control.cancelled())
					return;
				if (AvidMediaLayout::isInsideUmeRoot(location.path) || AvidMediaLayout::isInsideUmeRoot(path))
				{
					report(location.path, QStringLiteral("UME media is outside the supported scan scope"));
					return;
				}
				const bool tree = location.kind == ScanLocation::Kind::MediaTree;
				const bool omf = AvidMediaLayout::isOmfRoot(path);
				if (tree && omf && !m_request.includeOmf)
					return;
				const QFileInfo info(path);
				if (!info.isDir() || !info.isReadable())
				{
					report(location.path, QStringLiteral("Requested directory is unavailable or unreadable"));
					return;
				}
				if (m_control.cancelled())
					return;
				const QString label = QDir(location.path).dirName();
				if (!tree)
					prepareBase(location, path, label);
				else if (isAvidTree(path))
				{
					const auto problemsBefore = m_result.problems.size();
					const QString mxf = namedChild(path, Conventions::kMxfDir);
					if (!mxf.isEmpty())
						addRoot(mxf, path, label);
					else if (!m_control.cancelled() && m_result.problems.size() == problemsBefore)
						report(location.path, QStringLiteral("No MXF media root is present in the selected Avid MediaFiles folder"));
				}
				else if (omf)
					addRoot(path, info.absolutePath(), label);
				else
					report(location.path, QStringLiteral("Select an Avid MediaFiles or OMFI MediaFiles tree"));
			}

			// MARK: - Media folder discovery

			QVector<QString> childFolders(const QString &root)
			{
				discovering(root);
				QVector<QString> folders;
				QString problem;
				walkDirectory(root, m_control, problem, [&](const fs::path &path)
							  {
					const QFileInfo info(path);
					if (Conventions::isDotHidden(info.fileName()))
						return true;
					const auto status = visibleStatus(path, info, m_control, problem);
					if (status && fs::is_directory(*status) && AvidMediaLayout::locateMediaFolder(info.absoluteFilePath()))
						folders.append(info.absoluteFilePath());
					return true; });
				if (!problem.isEmpty())
					report(root, problem);
				std::sort(folders.begin(), folders.end(), nameBefore);
				return folders;
			}

			void walkRoot(const PreparedRoot &root)
			{
				discovering(root.path);
				if (m_control.cancelled())
					return;
				const QFileInfo info(root.path);
				if (!info.isDir() || !info.isReadable() || info.isSymLink())
				{
					report(root.path, QStringLiteral("Requested directory is unavailable, unreadable or a symlink"));
					return;
				}
				if (AvidMediaLayout::isOmfRoot(root.path))
				{
					if (!m_request.includeOmf)
						return;
					walkMediaFolder(root.path, root);
				}
				for (const auto &folder : childFolders(root.path))
				{
					if (m_control.cancelled())
						break;
					walkMediaFolder(folder, root);
				}
			}

			const PreparedRoot &displayContext(const QString &folder, const PreparedRoot &root) const
			{
				for (const auto &candidate : m_roots)
					if (folder == candidate.path || folder.startsWith(candidate.path + QLatin1Char('/')))
						return candidate;
				return root;
			}

			QString volumeIdentifier(const QString &folder)
			{
				if (m_control.cancelled())
					return {};
				const QString volumeRoot = QStorageInfo(folder).rootPath();
				if (m_control.cancelled())
					return {};
				const auto existing = m_volumeIdentifiers.constFind(volumeRoot);
				if (existing != m_volumeIdentifiers.cend())
					return existing.value();
				const QString identity = VolumeIdentity::capture(folder).identifier();
				m_volumeIdentifiers.insert(volumeRoot, identity);
				return identity;
			}

			void walkMediaFolder(const QString &folder, const PreparedRoot &root)
			{
				if (m_visitedFolders.contains(folder))
					return;
				const auto layout = AvidMediaLayout::locateMediaFolder(folder);
				if (!layout)
					return;
				m_visitedFolders.insert(folder);
				discovering(folder);
				const bool omf = layout->family == AvidMediaLayout::Family::Omf;
				const auto &context = displayContext(folder, root);
				const QString identity = volumeIdentifier(folder);
				const auto firstEntry = m_result.entries.size();
				const auto firstPmr = m_databases.pmrs.size();
				const auto firstMdb = m_databases.mdbs.size();
				QString problem;
				const bool finished = walkDirectory(folder, m_control, problem, [&](const fs::path &path)
													{
					const QFileInfo info(path);
					const QString name = info.fileName();
					if (Conventions::isDotHidden(name))
						return true;
					const bool pmr = name.endsWith(QLatin1String(".pmr"), Qt::CaseInsensitive);
					const bool mdb = name.endsWith(QLatin1String(".mdb"), Qt::CaseInsensitive);
					const bool media = omf ? Conventions::hasOmfEraExtension(name) : Conventions::hasMxfExtension(name);
					if (!pmr && !mdb && !media)
						return true;
					const auto status = visibleStatus(path, info, m_control, problem);
					if (!status || !fs::is_regular_file(*status))
						return true;
					const QString absolutePath = info.absoluteFilePath();
					if (m_capturedPaths.contains(absolutePath))
						return true;
					m_capturedPaths.insert(absolutePath);
					if (pmr)
					{
						PmrReport source;
						source.source.path = absolutePath;
						m_databases.pmrs.append(std::move(source));
					}
					else if (mdb)
					{
						MdbReport source;
						source.path = absolutePath;
						m_databases.mdbs.append(std::move(source));
					}
					else
					{
						ScanEntry entry;
						entry.path = absolutePath;
						entry.volumeIdentifier = identity;
						entry.volumePath = context.volumePath;
						entry.volumeName = context.volumeName;
						entry.omfEra = omf;
						entry.quarantined = layout->isQuarantined;
						if (!m_control.cancelled())
							entry.sizeBytes = fileSize(path);
						if (!m_control.cancelled())
							entry.created = info.birthTime();
						if (!m_control.cancelled())
							entry.modified = info.lastModified();
						m_result.entries.append(std::move(entry));
					}
					return true; });

				// The captured prefix remains usable after a failed listing or Cancel.
				// Ordering and row identity assignment need only the captured values.
				auto entries = m_result.entries.begin() + firstEntry;
				std::sort(entries, m_result.entries.end(), [](const ScanEntry &a, const ScanEntry &b)
						  { return nameBefore(a.path, b.path); });
				for (auto current = entries; current != m_result.entries.end(); ++current)
					current->kelpieId = m_ids.allocate();
				std::sort(m_databases.pmrs.begin() + firstPmr, m_databases.pmrs.end(), [](const PmrReport &a, const PmrReport &b)
						  { return nameBefore(a.source.path, b.source.path); });
				std::sort(m_databases.mdbs.begin() + firstMdb, m_databases.mdbs.end(), [](const MdbReport &a, const MdbReport &b)
						  { return nameBefore(a.path, b.path); });
				if (!problem.isEmpty())
					report(folder, problem);
				else if (finished)
					m_databases.completedFolders.insert(folder);
			}

			const ScanRequest &m_request;
			ScanControl &m_control;
			ScanResult &m_result;
			DiscoveredDatabases &m_databases;
			const ScanCallbacks &m_callbacks;
			bool m_complete = true;
			KelpieIdAllocator m_ids;
			QVector<PreparedRoot> m_roots;
			QSet<QString> m_rootPaths;
			QSet<QString> m_visitedFolders;
			QSet<QString> m_capturedPaths;
			QHash<QString, QString> m_volumeIdentifiers;
		};
	}

	// MARK: - Discovery entry point

	void discover(const ScanRequest &request, ScanControl &control, ScanResult &result,
				  DiscoveredDatabases &databases, const ScanCallbacks &callbacks)
	{
		DiscoveryRun(request, control, result, databases, callbacks).run();
	}
}
