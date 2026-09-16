#include "opfile.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#ifdef Q_OS_MAC
#include <QProcess>
#include <sys/acl.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace
{
using Sync = NativeFile::SyncResult;

struct Fixture
{
	QTemporaryDir temporary;
	QString root = QFileInfo(temporary.path()).canonicalFilePath();
	QString stage = root + "/.mediamuster-stage-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString partial = stage + "/payload.partial";
	OpStamp directory;
	QString error;
	bool create()
	{
		return OpFile::makePrivateDirectory(stage, directory, error) == Sync::Ok;
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

class TestOpCleanup : public QObject
{
	Q_OBJECT
private slots:
	void partial_can_be_removed_after_reopening()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(f.directory.sameObject(OpFile::inspectDirectory(f.stage)));
		QVERIFY(put(f.partial, "unfinished copy"));
		const auto recorded = OpFile::inspect(f.partial);
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY2(partial, qPrintable(f.error));
		QVERIFY2(partial->removePartial(recorded, f.directory, f.error), qPrintable(f.error));
		QVERIFY(!OpFile::occupied(f.partial));
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
		QVERIFY(!OpFile::occupied(f.stage));
	}

	void existing_directory_is_not_adopted()
	{
		Fixture f;
		QVERIFY(QDir().mkdir(f.stage));
		QVERIFY(put(f.partial, "existing"));
		QCOMPARE(OpFile::makePrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(!f.directory.valid());
		QVERIFY(OpFile::occupied(f.partial));
	}

	void creation_flush_failure_keeps_identity()
	{
		Fixture f;
		QCOMPARE(OpFile::makePrivateDirectory(f.stage, f.directory, f.error,
			[](const QString &, QString *error) {
				*error = QStringLiteral("injected persistence failure");
				return Sync::Failed;
			}), Sync::Failed);
		QVERIFY(f.directory.sameObject(OpFile::inspectDirectory(f.stage)));
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
	}

	void changed_partial_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OpFile::inspect(f.partial);
		QVERIFY(put(f.partial, "changed outside the operation"));
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, f.directory, f.error));
		QVERIFY(OpFile::occupied(f.partial));
	}

	void replaced_partial_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OpFile::inspect(f.partial);
		QVERIFY(QFile::rename(f.partial, f.stage + "/recorded.partial"));
		QVERIFY(put(f.partial, "replacement"));
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, f.directory, f.error));
		QVERIFY(OpFile::occupied(f.partial));
		QVERIFY(OpFile::occupied(f.stage + "/recorded.partial"));
	}

	void partial_requires_recorded_directory()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		const auto recorded = OpFile::inspect(f.partial);
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(recorded, OpFile::inspectDirectory(f.root), f.error));
		QVERIFY(!partial->removePartial(recorded, {}, f.error));
		QVERIFY(OpFile::occupied(f.partial));
	}

	void occupied_directory_keeps_every_child()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.stage + "/unexpected.txt", "unrelated contents"));
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OpFile::occupied(f.stage + "/unexpected.txt"));
	}

	void replaced_empty_directory_is_retained()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(QDir().rename(f.stage, f.root + "/saved-stage"));
		OpStamp replacement;
		QCOMPARE(OpFile::makePrivateDirectory(f.stage, replacement, f.error), Sync::Ok);
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OpFile::occupied(f.stage));
		QVERIFY(OpFile::occupied(f.root + "/saved-stage"));
	}

	void ordinary_folder_is_not_cleanup_scope()
	{
		Fixture f;
		const QString ordinary = f.root + "/ordinary";
		QVERIFY(QDir().mkdir(ordinary));
		const auto identity = OpFile::inspectDirectory(ordinary);
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(ordinary, identity, f.error), Sync::Failed);
		QVERIFY(put(ordinary + "/payload.partial", "ordinary file"));
		auto partial = OpFile::open(ordinary + "/payload.partial", false, f.error);
		QVERIFY(partial);
		QVERIFY(!partial->removePartial(partial->stamp(), identity, f.error));
		QVERIFY(OpFile::occupied(ordinary + "/payload.partial"));
	}

	void removed_directory_flush_failure_is_reported()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		int barriers = 0;
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error,
			[&](const QString &path, QString *error) {
				if (path == f.root)
					++barriers;
				*error = QStringLiteral("injected persistence failure");
				return Sync::Failed;
			}), Sync::Failed);
		QCOMPARE(barriers, 1);
		QVERIFY(!OpFile::occupied(f.stage));
	}

#ifndef Q_OS_WIN
	void broadened_permissions_retain_partial()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(::chmod(QFile::encodeName(f.stage).constData(), 0777) == 0);
		QVERIFY(!partial->removePartial(partial->stamp(), f.directory, f.error));
		QVERIFY(OpFile::occupied(f.partial));
	}

	void redirected_directory_is_not_followed()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		const auto saved = f.root + "/saved-stage";
		QVERIFY(QDir().rename(f.stage, saved));
		QVERIFY(QFile::link(saved, f.stage));
		QVERIFY(!OpFile::inspectDirectory(f.stage).valid());
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Failed);
		QVERIFY(OpFile::occupied(f.stage));
		QVERIFY(OpFile::occupied(saved));
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
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY2(partial->removePartial(partial->stamp(), f.directory, f.error), qPrintable(f.error));
		QCOMPARE(OpFile::removeEmptyPrivateDirectory(f.stage, f.directory, f.error), Sync::Ok);
	}

	void added_acl_prevents_partial_removal()
	{
		Fixture f;
		QVERIFY2(f.create(), qPrintable(f.error));
		QVERIFY(put(f.partial, "recorded"));
		auto partial = OpFile::open(f.partial, false, f.error);
		QVERIFY(partial);
		QVERIFY(addDirectoryAcl(f.stage, false));
		QVERIFY(hasExtendedAcl(f.stage));
		QVERIFY(!partial->removePartial(partial->stamp(), f.directory, f.error));
		QVERIFY(OpFile::occupied(f.partial));
	}
#endif
};

QTEST_GUILESS_MAIN(TestOpCleanup)
#include "tst_opcleanup.moc"
