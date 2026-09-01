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
	void normalise_strips_trailing_slash();
	void normalise_preserves_root_slash();
	void normalise_resolves_dot_components();
	void normalise_resolves_dotdot_components();
	void normalise_equates_trailing_slash_variants();
	void normalise_equates_dot_and_plain_forms();
	void normalise_non_existent_path_falls_back_to_absolute();
	void normalise_case_folds_on_case_insensitive_platforms();
	void normalise_key_is_stable_when_the_file_appears();
	void normalise_key_is_stable_through_a_symlinked_parent();
};

void TestPathKey::normalise_empty_input_returns_empty()
{
	QCOMPARE(PathKey::normalise(QString()), QString());
}

void TestPathKey::normalise_strips_trailing_slash()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// canonicalFilePath itself doesn't leave a trailing slash on a
	// real directory, but the input might. Either way, output should
	// not end with '/'.
	const QString withSlash = tmp.path() + QLatin1Char('/');
	const QString out = PathKey::normalise(withSlash);
	QVERIFY(!out.endsWith(QLatin1Char('/')));
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

void TestPathKey::normalise_resolves_dot_components()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString plain = tmp.path();
	const QString withDot = tmp.path() + QStringLiteral("/.");
	QCOMPARE(PathKey::normalise(withDot), PathKey::normalise(plain));
}

void TestPathKey::normalise_resolves_dotdot_components()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("child")));
	// "<tmp>/child/.." should normalise to "<tmp>".
	const QString viaDotdot = tmp.path() + QStringLiteral("/child/..");
	QCOMPARE(PathKey::normalise(viaDotdot), PathKey::normalise(tmp.path()));
}

void TestPathKey::normalise_equates_trailing_slash_variants()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString plain = tmp.path();
	const QString withSlash = tmp.path() + QLatin1Char('/');
	QCOMPARE(PathKey::normalise(plain), PathKey::normalise(withSlash));
}

void TestPathKey::normalise_equates_dot_and_plain_forms()
{
	// The whole point of normalise: differently-spelled paths that
	// point to the same place should compare equal after the call.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("a/b")));
	const QString direct = tmp.path() + QStringLiteral("/a/b");
	const QString indirect = tmp.path() + QStringLiteral("/a/./b/../b");
	QCOMPARE(PathKey::normalise(direct), PathKey::normalise(indirect));
}

void TestPathKey::normalise_non_existent_path_falls_back_to_absolute()
{
	// Path that doesn't exist on disk. canonicalFilePath returns
	// empty; normalise must fall back to absoluteFilePath rather
	// than silently dropping the input. Rooted via QDir::rootPath()
	// ("/" on Unix, "C:/" on Windows) so the expectation is portable:
	// an already-absolute, dot-free input survives the fallback —
	// modulo the case-fold keys get on Windows/macOS (which lowercases
	// even the drive letter; that cost this pin its first Windows run).
	const QString nope = QDir::rootPath() + QStringLiteral("this/definitely/does/not/exist");
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
	QCOMPARE(PathKey::normalise(nope), nope.toCaseFolded());
#else
	QCOMPARE(PathKey::normalise(nope), nope);
#endif
}


// Two spellings of one on-disk name must produce ONE key on the
// platforms whose default filesystems are case-insensitive — Windows'
// canonicalFilePath keeps the caller's spelling (unlike macOS, which
// resolves to the on-disk casing), and that mismatch made a case-variant
// flatten silently skip a file on the first Windows CI run.
void TestPathKey::normalise_case_folds_on_case_insensitive_platforms()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
	QCOMPARE(PathKey::normalise(QStringLiteral("/tmp/nonexistent/CLIP.MXF")),
			 PathKey::normalise(QStringLiteral("/tmp/nonexistent/clip.mxf")));
#else
	QSKIP("Case-sensitive platform: keys keep their casing.");
#endif
}

// THE invariant these keys exist for: the answer must not change the
// instant the file turns up. The runner inserts a destination's key
// before it writes the file and looks the same path up again after, so a
// key that moves under it means the lookup misses — which is how the
// second of two same-named files came to be silently skipped instead of
// diverted to "name (2)".
void TestPathKey::normalise_key_is_stable_when_the_file_appears()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + QStringLiteral("/clip.mxf");

	const QString beforeCreate = PathKey::normalise(dst);
	writeProbe(dst);
	const QString afterCreate = PathKey::normalise(dst);

	QCOMPARE(afterCreate, beforeCreate);

	// And the same for a whole missing branch, which is what a Copy into a
	// destination folder that does not exist yet looks like: the folder is
	// created between the two calls.
	const QString deep = tmp.path() + QStringLiteral("/made/later/clip.mxf");
	const QString beforeMkdir = PathKey::normalise(deep);
	QVERIFY(QDir().mkpath(QFileInfo(deep).absolutePath()));
	writeProbe(deep);
	QCOMPARE(PathKey::normalise(deep), beforeMkdir);
}

// The same invariant where it actually broke: any symlink in the path
// ("/tmp" -> "/private/tmp" on macOS, or a user's own symlink to a
// project drive) makes the canonical and absolute spellings differ, so a
// key computed on one side of the file's creation did not match the other.
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

	// Both spellings of the one folder still key the same, which is the
	// property canonicalisation was there for in the first place.
	QCOMPARE(PathKey::normalise(viaLink),
			 PathKey::normalise(real + QStringLiteral("/clip.mxf")));
#endif
}

QTEST_APPLESS_MAIN(TestPathKey)
#include "tst_pathkey.moc"