#include "canon/discoveryengine.h"
#include "canon/sourcereader.h"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	bool put(const QString &path)
	{
		if (!QDir().mkpath(QFileInfo(path).absolutePath()))
			return false;
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write("opaque bytes") == 12;
	}
}

class TestCanonDiscovery : public QObject
{
	Q_OBJECT
private slots:
	void optional_real_discovery()
	{
		const QString roots = qEnvironmentVariable("MEDIAMUSTER_CANON_REAL_SCAN_ROOTS");
		if (roots.isEmpty())
			QSKIP("Opt-in read-only discovery of real managed roots");
		Canon::Cancellation cancellation;
		const auto result = Canon::DiscoveryEngine{}.discover({roots.split(';', Qt::SkipEmptyParts), true}, cancellation);
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
			QCOMPARE(file.evidence.selected(MediaProperty::Codec).readState, PropertyReadState::NotRead);
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
		Canon::Cancellation cancellation;
		const auto result = Canon::DiscoveryEngine{}.discover({{base}, true}, cancellation);
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
			QCOMPARE(file.evidence.selected(MediaProperty::Codec).readState, PropertyReadState::NotRead);
			QCOMPARE(file.evidence.observations(MediaProperty::Size).first().value.toLongLong(), qint64(12));
		}
		QCOMPARE(legacy, 3);
		QCOMPARE(quarantined, 1);
		const auto mxfOnly = Canon::DiscoveryEngine{}.discover({{base}, false}, cancellation);
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
		Canon::Cancellation cancellation;
		const auto result = Canon::DiscoveryEngine{}.discover({{temp.path(), mxf, mxf + "/1"}, true}, cancellation);
		QVERIFY(result.discoveryComplete);
		QCOMPARE(result.files.size(), 2);
		QVERIFY(result.files[0].path != result.files[1].path);
		QVERIFY(result.files[0].kelpieId != result.files[1].kelpieId);
		const auto repeat = Canon::DiscoveryEngine{}.discover({{mxf}, true}, cancellation);
		QCOMPARE(repeat.files.first().kelpieId, KelpieId(1));
	}

	void unavailable_and_unmanaged_scope_remains_qualified()
	{
		QTemporaryDir temp;
		QVERIFY(put(temp.path() + "/deep/project/Avid MediaFiles/MXF/1/video.mxf"));
		Canon::Cancellation cancellation;
		const auto result = Canon::DiscoveryEngine{}.discover({{temp.path(), temp.path() + "/absent"}, true}, cancellation);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(result.files.isEmpty());
		QCOMPARE(result.discoveryIssues.size(), 2);
		QCOMPARE(result.discoveryIssues[0].kind, Canon::DiscoveryIssue::Kind::UnmanagedRoot);
		QCOMPARE(result.discoveryIssues[1].kind, Canon::DiscoveryIssue::Kind::UnavailableRoot);
	}

	void cancellation_does_not_claim_completion()
	{
		Canon::Cancellation cancellation;
		cancellation.cancel();
		const auto result = Canon::DiscoveryEngine{}.discover({{QStringLiteral("/")}, true}, cancellation);
		QVERIFY(result.cancelled);
		QVERIFY(!result.discoveryComplete);
		QVERIFY(result.files.isEmpty());
	}

};

QTEST_GUILESS_MAIN(TestCanonDiscovery)
#include "tst_canondiscovery.moc"
