#include "pathkey.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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

QTEST_APPLESS_MAIN(TestPathKey)
#include "tst_pathkey.moc"