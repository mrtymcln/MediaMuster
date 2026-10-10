#include "mediaengine/discoveryengine.h"
#include "mediaengine/discoveryengine_p.h"
#include "mediaengine/sourcereader.h"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
	bool put(const QString &path)
	{
		if (!QDir().mkpath(QFileInfo(path).absolutePath()))
			return false;
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write("opaque bytes") == 12;
	}

#ifdef Q_OS_UNIX
	bool hasUnreadableIssue(const MediaEngine::ScanResult &result, const QString &folder)
	{
		for (const auto &issue : result.discoveryIssues)
			if (issue.kind == MediaEngine::DiscoveryIssue::Kind::UnreadableFolder &&
				(issue.path == folder || issue.path.startsWith(folder + '/')) && !issue.explanation.isEmpty())
				return true;
		return false;
	}

	class DirectoryPermissions
	{
	public:
		explicit DirectoryPermissions(const QString &path) : m_path(QFile::encodeName(path))
		{
			struct stat info{};
			m_saved = ::stat(m_path.constData(), &info) == 0;
			m_original = info.st_mode & 07777;
		}
		~DirectoryPermissions()
		{
			if (m_saved)
				::chmod(m_path.constData(), m_original);
		}
		bool set(mode_t mode) const { return m_saved && ::chmod(m_path.constData(), mode) == 0; }
		Q_DISABLE_COPY_MOVE(DirectoryPermissions)

	private:
		QByteArray m_path;
		mode_t m_original = 0;
		bool m_saved = false;
	};
#endif
}

class TestMediaEngineDiscovery : public QObject
{
	Q_OBJECT
private slots:
	void unavailable_filesystem_metadata_is_an_attempted_read()
	{
		MediaEngine::MediaFile file;
		const auto source = SourceSnapshotRef::create(SourceSnapshot{
			MetadataSource::Filesystem, QStringLiteral("file.mxf"), {}, SourceReadState::Complete});
		for (const auto property : {MediaProperty::Created, MediaProperty::Modified, MediaProperty::VolumeIdentifier})
		{
			MediaEngine::Detail::filesystemObservation(file, source, property, QStringLiteral("tested filesystem property"), {});
			const auto &observation = file.evidence.observations(property).first();
			QCOMPARE(observation.readState, PropertyReadState::Unreadable);
			QVERIFY(!observation.value.isValid());
			QVERIFY(!observation.explanation.isEmpty());
			QCOMPARE(observation.snapshot, source);
		}
		// Zero and false are usable values; neither means that a query failed.
		MediaEngine::Detail::filesystemObservation(file, source, MediaProperty::Size, QStringLiteral("byte size"), qint64(0));
		MediaEngine::Detail::filesystemObservation(file, source, MediaProperty::OmfScan, QStringLiteral("legacy family"), false);
		QCOMPARE(file.evidence.observations(MediaProperty::Size).first().readState, PropertyReadState::Present);
		QCOMPARE(file.evidence.observations(MediaProperty::Size).first().value.toLongLong(), qint64(0));
		QCOMPARE(file.evidence.observations(MediaProperty::OmfScan).first().readState, PropertyReadState::Present);
		QCOMPARE(file.evidence.observations(MediaProperty::OmfScan).first().value.toBool(), false);
		QVERIFY(file.evidence.observations(MediaProperty::Compression).isEmpty());
		QCOMPARE(file.evidence.selected(MediaProperty::Compression).readState, PropertyReadState::NotRead);
	}

	void unicode_names_and_symlink_exclusions()
	{
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString root = temp.path() + "/Avid MediaFiles/MXF";
		const QString folder = root + QString::fromUtf8("/No\u0308n English \u4f60\u597d");
		const QString filename = QString::fromUtf8("/na\u0301me\u2122 \u6f22.MXF");
		QVERIFY(put(folder + filename));
#ifdef Q_OS_UNIX
		QVERIFY(QFile::link(folder + filename, folder + "/linked.mxf"));
		QVERIFY(QFile::link(folder, root + "/Linked folder"));
#endif
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{root}, true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QVERIFY(result.discoveryIssues.isEmpty());
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.candidates.size(), 1);
		QCOMPARE(QFileInfo(result.files.first().path).canonicalFilePath(), QFileInfo(folder + filename).canonicalFilePath());
	}

	void optional_real_discovery()
	{
		const QString roots = qEnvironmentVariable("MEDIAMUSTER_MEDIAENGINE_REAL_SCAN_ROOTS");
		if (roots.isEmpty())
			QSKIP("Opt-in read-only discovery of real managed roots");
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({roots.split(';', Qt::SkipEmptyParts), true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QVERIFY(!result.files.isEmpty());
		QSet<QString> paths;
		QSet<KelpieId> ids;
		for (const auto &file : result.files)
		{
			QVERIFY(!paths.contains(file.path));
			QVERIFY(file.kelpieId != 0 && !ids.contains(file.kelpieId));
			paths.insert(file.path);
			ids.insert(file.kelpieId);
			QCOMPARE(file.evidence.selected(MediaProperty::Compression).readState, PropertyReadState::NotRead);
		}
		qInfo() << "Fresh discovery:" << result.files.size() << "physical rows," << result.candidates.size() << "parser candidates";
	}

	void scope_is_flat_and_extension_based()
	{
		QTemporaryDir temp;
		const QString base = temp.path();
		for (const QString &name : {QStringLiteral("Avid MediaFiles/MXF/1/video.MXF"),
			QStringLiteral("Avid MediaFiles/MXF/Named/copy.mxf"),
			QStringLiteral("Avid MediaFiles/MXF/Quarantined Files/q.mxf"),
			QStringLiteral("Avid MediaFiles/MXF/1/future.PMR"),
			QStringLiteral("Avid MediaFiles/MXF/1/future.MDB"),
			QStringLiteral("Avid MediaFiles/MXF/1/ignored.wav"),
			QStringLiteral("Avid MediaFiles/MXF/1/Nested/deep.mxf"),
			QStringLiteral("Avid MediaFiles/MXF/Creating/staging.mxf"),
			QStringLiteral("Avid MediaFiles/MXF/.hidden/hidden.mxf"),
			QStringLiteral("Avid MediaFiles/MXF/1/.hidden.mxf"),
			QStringLiteral("Avid MediaFiles/UME/file.mxf"),
			QStringLiteral("OMFI MediaFiles/root.wav"),
			QStringLiteral("OMFI MediaFiles/Editor/file.aif"),
			QStringLiteral("OMFI MediaFiles/2/file.omf"),
			QStringLiteral("OMFI MediaFiles/2/ignored.mxf"),
			QStringLiteral("OMFI MediaFiles/2/future.pmr"),
			QStringLiteral("OMFI MediaFiles/2/Nested/ignored.omf")})
			QVERIFY(put(base + '/' + name));
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{base}, true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QVERIFY(!result.parsingComplete && !result.reconciliationComplete);
		QCOMPARE(result.files.size(), 6);
		QCOMPARE(result.candidates.size(), 9);
		QSet<KelpieId> ids;
		int legacy = 0, quarantined = 0;
		for (const auto &file : result.files)
		{
			QVERIFY(file.kelpieId != 0 && !ids.contains(file.kelpieId));
			ids.insert(file.kelpieId);
			legacy += int(file.omfScan);
			quarantined += int(file.quarantined);
			QCOMPARE(file.stamp.path, file.path);
			QVERIFY(file.stamp.mobId.isEmpty());
			QCOMPARE(file.evidence.selected(MediaProperty::Compression).readState, PropertyReadState::NotRead);
			QCOMPARE(file.evidence.observations(MediaProperty::Size).first().value.toLongLong(), qint64(12));
		}
		QCOMPARE(legacy, 3);
		QCOMPARE(quarantined, 1);
		const auto mxfOnly = MediaEngine::DiscoveryEngine{}.discover({{base}, false}, cancellation);
		QCOMPARE(mxfOnly.files.size(), 3);
		QCOMPARE(mxfOnly.candidates.size(), 5);
		QVERIFY(mxfOnly.discoveryComplete);
	}

	void overlapping_requests_do_not_merge_different_locations()
	{
		QTemporaryDir temp;
		const QString mxf = temp.path() + "/Avid MediaFiles/MXF";
		QVERIFY(put(mxf + "/1/same.mxf"));
		QVERIFY(put(mxf + "/2/same.mxf"));
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{temp.path(), mxf, mxf + "/1"}, true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QCOMPARE(result.files.size(), 2);
		QVERIFY(result.files[0].path != result.files[1].path);
		QVERIFY(result.files[0].kelpieId != result.files[1].kelpieId);
		const auto repeat = MediaEngine::DiscoveryEngine{}.discover({{mxf}, true}, cancellation);
		QCOMPARE(repeat.files.first().kelpieId, KelpieId(1));
	}

	void unavailable_and_unmanaged_scope_remains_qualified()
	{
		QTemporaryDir temp;
		QVERIFY(put(temp.path() + "/deep/project/Avid MediaFiles/MXF/1/video.mxf"));
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{temp.path(), temp.path() + "/absent"}, true}, cancellation);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(result.files.isEmpty());
		QCOMPARE(result.discoveryIssues.size(), 2);
		QCOMPARE(result.discoveryIssues[0].kind, MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot);
		QCOMPARE(result.discoveryIssues[1].kind, MediaEngine::DiscoveryIssue::Kind::UnavailableRoot);
	}

	void empty_managed_folders_are_complete()
	{
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		QVERIFY(QDir().mkpath(temp.path() + "/Avid MediaFiles/MXF/1"));
		QVERIFY(QDir().mkpath(temp.path() + "/OMFI MediaFiles/Editor"));
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{temp.path()}, true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QVERIFY(!result.cancelled);
		QVERIFY(result.files.isEmpty());
		QVERIFY(result.candidates.isEmpty());
		QVERIFY(result.discoveryIssues.isEmpty());
	}

	void folder_without_search_permission_does_not_claim_completion()
	{
#ifdef Q_OS_UNIX
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString root = temp.path() + "/Avid MediaFiles/MXF";
		const QString restricted = root + "/1";
		const QString unreachableFile = restricted + "/unreachable.mxf";
		const QString readableFile = root + "/2/readable.mxf";
		QVERIFY(put(unreachableFile));
		QVERIFY(put(readableFile));
		DirectoryPermissions permissions(restricted);
		QVERIFY(permissions.set(0400));
		if (::access(QFile::encodeName(unreachableFile).constData(), F_OK) == 0)
			QSKIP("Host bypasses directory search permissions");
		QCOMPARE(errno, EACCES);
		QVERIFY(QFileInfo(restricted).isReadable());
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{root}, true}, cancellation);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(!result.cancelled);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().path, readableFile);
		QCOMPARE(result.candidates.size(), 1);
		QVERIFY(hasUnreadableIssue(result, restricted));
#else
		QSKIP("POSIX directory permission regression");
#endif
	}

	void unreadable_avid_container_preserves_other_family()
	{
#ifdef Q_OS_UNIX
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString restricted = temp.path() + "/Avid MediaFiles";
		const QString readableFile = temp.path() + "/OMFI MediaFiles/readable.omf";
		QVERIFY(put(restricted + "/MXF/1/unreachable.mxf"));
		QVERIFY(put(readableFile));
		DirectoryPermissions permissions(restricted);
		QVERIFY(permissions.set(0000));
		if (::access(QFile::encodeName(restricted).constData(), R_OK | X_OK) == 0)
			QSKIP("Host bypasses restrictive directory permissions");
		QCOMPARE(errno, EACCES);
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{temp.path()}, true}, cancellation);
		QVERIFY(!result.discoveryComplete);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().path, readableFile);
		QVERIFY(result.files.first().omfScan);
		QCOMPARE(result.candidates.size(), 1);
		QVERIFY(hasUnreadableIssue(result, restricted));
#else
		QSKIP("POSIX directory permission regression");
#endif
	}

	void unreadable_mxf_root_preserves_other_request()
	{
#ifdef Q_OS_UNIX
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString restricted = temp.path() + "/Avid MediaFiles/MXF";
		const QString omf = temp.path() + "/OMFI MediaFiles";
		const QString readableFile = omf + "/readable.wav";
		QVERIFY(put(restricted + "/1/unreachable.mxf"));
		QVERIFY(put(readableFile));
		DirectoryPermissions permissions(restricted);
		QVERIFY(permissions.set(0000));
		if (::access(QFile::encodeName(restricted).constData(), R_OK | X_OK) == 0)
			QSKIP("Host bypasses restrictive directory permissions");
		QCOMPARE(errno, EACCES);
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{temp.path() + "/Avid MediaFiles", omf}, true}, cancellation);
		QVERIFY(!result.discoveryComplete);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().path, readableFile);
		QCOMPARE(result.candidates.size(), 1);
		QVERIFY(hasUnreadableIssue(result, restricted));
#else
		QSKIP("POSIX directory permission regression");
#endif
	}

	void folder_progress_precedes_enumeration()
	{
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString root = temp.path() + "/Avid MediaFiles/MXF";
		const QString folder = root + "/1";
		QVERIFY(QDir().mkpath(folder));
		MediaEngine::Cancellation cancellation;
		QStringList reported;
		bool created = false;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{root}, false}, cancellation,
			[&](const QString &path)
			{
				reported.append(path);
				if (path == folder)
					created = put(folder + "/arrived.mxf");
			});
		QVERIFY(created);
		QCOMPARE(reported.first(), root);
		QVERIFY(reported.contains(folder));
		QVERIFY(result.discoveryComplete);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().path, folder + "/arrived.mxf");
	}

	void cancellation_from_folder_progress_stops_enumeration()
	{
		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		const QString root = temp.path() + "/Avid MediaFiles/MXF";
		const QString folder = root + "/1";
		QVERIFY(put(folder + "/unread.mxf"));
		MediaEngine::Cancellation cancellation;
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{root}, false}, cancellation,
			[&](const QString &path)
			{
				if (path == folder)
					cancellation.cancel();
			});
		QVERIFY(result.cancelled);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(result.files.isEmpty());
		QVERIFY(result.candidates.isEmpty());
		QVERIFY(result.discoveryIssues.isEmpty());
	}

	void cancellation_does_not_claim_completion()
	{
		MediaEngine::Cancellation cancellation;
		cancellation.cancel();
		const auto result = MediaEngine::DiscoveryEngine{}.discover({{QStringLiteral("/")}, true}, cancellation);
		QVERIFY(result.cancelled);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(result.files.isEmpty());
	}

};

QTEST_GUILESS_MAIN(TestMediaEngineDiscovery)
#include "tst_mediaenginediscovery.moc"
