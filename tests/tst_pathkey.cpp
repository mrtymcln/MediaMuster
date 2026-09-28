#include "pathkey.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	void writeProbe(const QString &path)
	{
		QFile f(path);
		QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
		f.write("x", 1);
	}
} // namespace

class TestPathKey : public QObject
{
	Q_OBJECT
private slots:
	void normalise_empty_input_returns_empty();
	void normalise_preserves_root_slash();
	void normalise_equates_path_spellings_data();
	void normalise_equates_path_spellings();
	void normalise_non_existent_path_falls_back_to_absolute();
	void normalise_case_folds_on_case_insensitive_platforms();
	void normalise_key_is_stable_when_the_file_appears();
	void normalise_key_is_stable_through_a_symlinked_parent();
};

void TestPathKey::normalise_empty_input_returns_empty()
{
	QCOMPARE(PathKey::normalise(QString()), QString());
}

void TestPathKey::normalise_preserves_root_slash()
{
#ifdef Q_OS_WIN
	// Windows has no bare "/" root — QFileInfo("/") resolves to the current
	// drive. The equivalent guarantee there: a drive root keys identically
	// however it's spelled (slash direction, trailing separator or not).
	QCOMPARE(PathKey::normalise(QStringLiteral("C:/")),
			 PathKey::normalise(QStringLiteral("C:\\")));
	QVERIFY(!PathKey::normalise(QStringLiteral("C:/")).isEmpty());
#else
	// "/" should normalise to "/", not "".
	QCOMPARE(PathKey::normalise(QStringLiteral("/")), QStringLiteral("/"));
#endif
}

void TestPathKey::normalise_equates_path_spellings_data()
{
	QTest::addColumn<QString>("plain");
	QTest::addColumn<QString>("alternate");
	QTest::newRow("trailing-slash") << QString{} << QStringLiteral("/");
	QTest::newRow("dot") << QString{} << QStringLiteral("/.");
	QTest::newRow("dotdot") << QString{} << QStringLiteral("/a/..");
	QTest::newRow("combined") << QStringLiteral("/a/b") << QStringLiteral("/a/./b/../b");
}

void TestPathKey::normalise_equates_path_spellings()
{
	QFETCH(QString, plain);
	QFETCH(QString, alternate);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("a/b")));
	const QString key = PathKey::normalise(tmp.path() + alternate);
	QCOMPARE(key, PathKey::normalise(tmp.path() + plain));
	QVERIFY(!key.endsWith(QLatin1Char('/')));
}

void TestPathKey::normalise_non_existent_path_falls_back_to_absolute()
{
	// Missing paths retain their absolute spelling, with platform case folding.
	const QString nope = QDir::rootPath() + QStringLiteral("this/definitely/does/not/exist");
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
	QCOMPARE(PathKey::normalise(nope), nope.toCaseFolded());
#else
	QCOMPARE(PathKey::normalise(nope), nope);
#endif
}

// Case variants must collide even before the destination file exists.
void TestPathKey::normalise_case_folds_on_case_insensitive_platforms()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
	QCOMPARE(PathKey::normalise(QStringLiteral("/tmp/nonexistent/CLIP.MXF")),
			 PathKey::normalise(QStringLiteral("/tmp/nonexistent/clip.mxf")));
#else
	QSKIP("Case-sensitive platform: keys keep their casing.");
#endif
}

// The runner inserts keys before writing and looks them up after creation.
void TestPathKey::normalise_key_is_stable_when_the_file_appears()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + QStringLiteral("/clip.mxf");

	const QString beforeCreate = PathKey::normalise(dst);
	writeProbe(dst);
	const QString afterCreate = PathKey::normalise(dst);

	QCOMPARE(afterCreate, beforeCreate);

	// Copy may also create the destination folder between lookups.
	const QString deep = tmp.path() + QStringLiteral("/made/later/clip.mxf");
	const QString beforeMkdir = PathKey::normalise(deep);
	QVERIFY(QDir().mkpath(QFileInfo(deep).absolutePath()));
	writeProbe(deep);
	QCOMPARE(PathKey::normalise(deep), beforeMkdir);
}

// A symlinked ancestor must not change the key when the leaf appears.
void TestPathKey::normalise_key_is_stable_through_a_symlinked_parent()
{
#ifdef Q_OS_WIN
	QSKIP("Windows has no POSIX symlink to build the fixture from.");
#else
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString real = tmp.path() + QStringLiteral("/real");
	const QString link = tmp.path() + QStringLiteral("/link");
	QVERIFY(QDir().mkpath(real));
	QVERIFY2(QFile::link(real, link), "could not create the symlink fixture");

	const QString viaLink = link + QStringLiteral("/clip.mxf");
	const QString beforeCreate = PathKey::normalise(viaLink);
	writeProbe(viaLink);
	const QString afterCreate = PathKey::normalise(viaLink);
	QCOMPARE(afterCreate, beforeCreate);

	// The symlink and real parent still identify the same destination.
	QCOMPARE(PathKey::normalise(viaLink),
			 PathKey::normalise(real + QStringLiteral("/clip.mxf")));
#endif
}

QTEST_APPLESS_MAIN(TestPathKey)
#include "tst_pathkey.moc"
