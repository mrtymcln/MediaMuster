#include "operations/operation_file.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#ifdef Q_OS_MAC
#include <sys/acl.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifndef Q_OS_WIN
#include <QProcess>
#include <sys/stat.h>
#endif

// MARK: - Test helpers

namespace
{
using Sync = FileSync::SyncResult;

struct Fixture
{
	QTemporaryDir temporary;
	QString root = QFileInfo(temporary.path()).canonicalFilePath();
	QString stage = root + "/.mediamuster-stage-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString partial = stage + "/payload.partial";
	OperationStamp directory;
	QString error;
	bool create()
	{
		return OperationFile::makePrivateDirectory(stage, directory, error) == Sync::Ok;
	}
};

bool put(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
#ifdef Q_OS_MAC
bool addDirectoryAcl(const QString &path, bool inherited)
{
	QProcess chmod;
	const QString permission = inherited
		? QStringLiteral("everyone allow list,search,add_file,directory_inherit")
		: QStringLiteral("everyone allow list,search,add_file");
	chmod.start(QStringLiteral("/bin/chmod"), {QStringLiteral("+a"), permission, path});
	return chmod.waitForFinished() && chmod.exitStatus() == QProcess::NormalExit && chmod.exitCode() == 0;
}

bool hasExtendedAcl(const QString &path)
{
	const int directory = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY);
	if (directory < 0)
		return false;
	acl_t acl = ::acl_get_fd_np(directory, ACL_TYPE_EXTENDED);
	acl_entry_t entry{};
	const bool present = acl && ::acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) == 0;
	if (acl)
		::acl_free(acl);
	::close(directory);
	return present;
}
#endif
}

// MARK: - Test cases

class TestOperationCleanup : public QObject
{
	Q_OBJECT
private slots:
	void partial_can_be_removed_after_reopening()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(f.directory.sameObject(OperationFile::inspectDirectory(f.stage)));
		QVERIFY(put(f.partial, "unfinished copy"));
		const auto recorded = OperationFile::inspect(f.partial);
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY2(partial, qPrintable(f.error));
		QVERIFY2(partial->removePartial(recorded, f.directory, f.error), qPrintable(f.error));
		QVERIFY(!OperationFile::occupied(f.partial));
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
		QVERIFY(!OperationFile::occupied(f.stage));
	}

	void existing_directory_is_not_adopted()
	{
		Fixture f;
		QVERIFY(QDir().mkdir(f.stage));
		QVERIFY(put(f.partial, "existing"));
		QCOMPARE(OperationFile::makePrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(!f.directory.valid());
		QVERIFY(OperationFile::occupied(f.partial));
	}

	void creation_flush_failure_keeps_identity()
	{
		Fixture f;
		QCOMPARE(OperationFile::makePrivateDirectory(f.stage, f.directory, f.error,
			[](const QString &, QString *error) {
				*error = QStringLiteral("injected persistence failure");
				return Sync::Failed;
			}), Sync::Failed);
		QVERIFY(f.directory.sameObject(OperationFile::inspectDirectory(f.stage)));
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
	}

	void changed_partial_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OperationFile::inspect(f.partial);
		QVERIFY(put(f.partial, "changed outside the operation"));
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, f.directory, f.error));
		QVERIFY(OperationFile::occupied(f.partial));
	}

	void replaced_partial_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OperationFile::inspect(f.partial);
		QVERIFY(QFile::rename(f.partial, f.stage + "/recorded.partial"));
		QVERIFY(put(f.partial, "replacement"));
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, f.directory, f.error));
		QVERIFY(OperationFile::occupied(f.partial));
		QVERIFY(OperationFile::occupied(f.stage + "/recorded.partial"));
	}

	void partial_requires_recorded_directory()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OperationFile::inspect(f.partial);
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, OperationFile::inspectDirectory(f.root), f.error));
		QVERIFY(!partial->removePartial(recorded, {}, f.error));
		QVERIFY(OperationFile::occupied(f.partial));
	}

	// MARK: - Directory ownership

	void occupied_directory_keeps_every_child()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.stage + "/unexpected.txt", "unrelated contents"));
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OperationFile::occupied(f.stage + "/unexpected.txt"));
	}

	void replaced_empty_directory_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(QDir().rename(f.stage, f.root + "/saved-stage"));
		OperationStamp replacement;
		QCOMPARE(OperationFile::makePrivateDirectory(f.stage, replacement, f.error), Sync::Ok);
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OperationFile::occupied(f.stage));
		QVERIFY(OperationFile::occupied(f.root + "/saved-stage"));
	}

	void ordinary_folder_is_not_cleanup_scope()
	{
		Fixture f;
		const QString ordinary = f.root + "/ordinary";
		QVERIFY(QDir().mkdir(ordinary));
		const auto identity = OperationFile::inspectDirectory(ordinary);
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(ordinary, identity, f.error), Sync::Failed);
		QVERIFY(put(ordinary + "/payload.partial", "ordinary file"));
		auto partial = OperationFile::open(ordinary + "/payload.partial", false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(partial->stamp(), identity, f.error));
		QVERIFY(OperationFile::occupied(ordinary + "/payload.partial"));
	}

	void removed_directory_flush_failure_is_reported()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		int barriers = 0;
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error,
			[&](const QString &path, QString *error) {
				if (path == f.root)
					++barriers;
				*error = QStringLiteral("injected persistence failure");
				return Sync::Failed;
			}), Sync::Failed);
		QCOMPARE(barriers, 1);
		QVERIFY(!OperationFile::occupied(f.stage));
	}

#ifndef Q_OS_WIN
	// MARK: - Native filesystem boundaries

	void named_pipe_is_rejected_without_waiting_for_a_writer()
	{
		Fixture f;
		const QString pipe = f.root + "/selected.mxf";
		QVERIFY(::mkfifo(QFile::encodeName(pipe).constData(), 0600) == 0);
		// Use a child so a blocking open fails this test without hanging the suite.
		QProcess probe;
		probe.start(QCoreApplication::applicationFilePath(), {"--reject-fifo", pipe});
		QVERIFY(probe.waitForStarted());
		const bool finished = probe.waitForFinished(3000);
		if (!finished)
		{
			probe.kill();
			probe.waitForFinished();
		}
		QVERIFY2(finished, "Opening a named pipe waited for a writer.");
		QCOMPARE(probe.exitStatus(), QProcess::NormalExit);
		QCOMPARE(probe.exitCode(), 0);
		struct stat info{};
		QVERIFY(::lstat(QFile::encodeName(pipe).constData(), &info) == 0);
		QVERIFY(S_ISFIFO(info.st_mode));
	}

	void broadened_permissions_retain_partial()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(::chmod(QFile::encodeName(f.stage).constData(), 0777) == 0);
		QVERIFY(!partial->removePartial(partial->stamp(), f.directory, f.error));
		QVERIFY(OperationFile::occupied(f.partial));
	}

	void redirected_directory_is_not_followed()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		const auto saved = f.root + "/saved-stage";
		QVERIFY(QDir().rename(f.stage, saved));
		QVERIFY(QFile::link(saved, f.stage));
		QVERIFY(!OperationFile::inspectDirectory(f.stage).valid());
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OperationFile::occupied(f.stage));
		QVERIFY(OperationFile::occupied(saved));
	}
#endif

#ifdef Q_OS_MAC
	void inherited_acl_is_removed_from_new_private_folder()
	{
		Fixture f;
		QVERIFY(addDirectoryAcl(f.root, true));
		const auto control = f.root + "/inherited-control";
		QVERIFY(QDir().mkdir(control));
		QVERIFY(hasExtendedAcl(control));
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(!hasExtendedAcl(f.stage));
		QVERIFY(put(f.partial, "recorded"));
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY2(partial->removePartial(partial->stamp(), f.directory, f.error), qPrintable(f.error));
		QCOMPARE(OperationFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
	}

	void added_acl_prevents_partial_removal()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		auto partial = OperationFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(addDirectoryAcl(f.stage, false));
		QVERIFY(hasExtendedAcl(f.stage));
		QVERIFY(!partial->removePartial(partial->stamp(), f.directory, f.error));
		QVERIFY(OperationFile::occupied(f.partial));
	}
#endif
};

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifndef Q_OS_WIN
	const auto arguments = app.arguments();
	if (arguments.size() == 3 && arguments[1] == "--reject-fifo")
	{
		QString error;
		const auto file = OperationFile::open(arguments[2], false, error);
		return !file && error == "Only regular files are supported." ? 0 : 1;
	}
#endif
	TestOperationCleanup test;
	return QTest::qExec(&test, argc, argv);
}
#include "tst_operation_cleanup.moc"
