#include "avbparser.h"
#include "mediatablemodel.h"
#include "mobid.h"

#include <QSignalSpy>
#include <QTest>

namespace
{
	QString masterId()
	{
		return MobId::format(QByteArray::fromHex(
			"060a2b340101010501010f1013000000443322116655887799aabbccddeeff00"));
	}

	AvbBin bin(const QString &name = QStringLiteral("Edited clip"),
			   const QString &originalBin = QStringLiteral("Original rushes"),
			   const QString &uid = QStringLiteral("0000000100000002"))
	{
		AvbBin value;
		value.valid = true;
		value.complete = true;
		value.filePath = QStringLiteral("/current/renamed-bin.avb");
		value.displayName = QStringLiteral("renamed-bin");
		AvbMob mob;
		mob.mobId = masterId();
		mob.name = name;
		mob.mobType = AvbMob::masterMobType;
		mob.originalBin = originalBin;
		mob.originalBinUid = uid;
		value.mobs.append(mob);
		return value;
	}

	MediaFile row(const QString &path = QStringLiteral("/media/clip.mxf"))
	{
		MediaFile file;
		file.filePath = path;
		file.masterMobId = MobId::toPmrForm(masterId());
		return file;
	}
}

class TestAvbMetadata : public QObject
{
	Q_OBJECT
private slots:
	void fills_missing_owned_metadata_in_both_identity_forms();
	void preserves_scanner_metadata_and_ignores_source_names();
	void conflicts_are_independent_and_retractable();
	void same_bin_name_with_different_uid_is_ambiguous();
	void incomplete_bins_cannot_supply_metadata();
	void rescans_and_removals_preserve_provenance();
};

void TestAvbMetadata::fills_missing_owned_metadata_in_both_identity_forms()
{
	MediaTableModel model;
	MediaFile little = row();
	little.masterMobId = masterId();
	model.setMediaFiles({little, row(QStringLiteral("/media/second.mxf"))});
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setAvbBins({bin()});
	QCOMPARE(changed.size(), 1);
	for (const MediaFile &file : model.allFiles())
	{
		QCOMPARE(file.clipName, QStringLiteral("Edited clip"));
		QCOMPARE(file.clipNameSource, MediaFile::ClipNameSource::Avb);
		QCOMPARE(file.originalBin, QStringLiteral("Original rushes"));
		QVERIFY(file.originalBinFromAvb);
	}
	model.setAvbBins({});
	for (const MediaFile &file : model.allFiles())
	{
		QVERIFY(file.clipName.isEmpty());
		QCOMPARE(file.clipNameSource, MediaFile::ClipNameSource::None);
		QVERIFY(file.originalBin.isEmpty());
		QVERIFY(!file.originalBinFromAvb);
	}
}

void TestAvbMetadata::preserves_scanner_metadata_and_ignores_source_names()
{
	MediaTableModel model;
	MediaFile known = row();
	known.clipName = QStringLiteral("Header clip");
	known.clipNameSource = MediaFile::ClipNameSource::MaterialPackage;
	known.originalBin = QStringLiteral("Recorded bin");
	model.setMediaFiles({known});
	model.setAvbBins({bin()});
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, known.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, known.clipNameSource);
	QCOMPARE(model.fileAt(0).originalBin, known.originalBin);
	QVERIFY(!model.fileAt(0).originalBinFromAvb);

	AvbBin source = bin();
	source.mobs[0].mobType = 3;
	model.setMediaFiles({row()});
	model.setAvbBins({source});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	MediaFile noMaster = row();
	noMaster.mobId = noMaster.masterMobId;
	noMaster.masterMobId.clear();
	model.setMediaFiles({noMaster});
	model.setAvbBins({bin()});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
}

void TestAvbMetadata::conflicts_are_independent_and_retractable()
{
	MediaTableModel model;
	model.setMediaFiles({row()});
	model.setAvbBins({bin(), bin(QStringLiteral("Another edit"))});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	model.setAvbBins({bin()});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QStringLiteral("Different bin"))});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QVERIFY(!model.fileAt(0).originalBinFromAvb);
}

void TestAvbMetadata::same_bin_name_with_different_uid_is_ambiguous()
{
	MediaTableModel model;
	model.setMediaFiles({row()});
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QStringLiteral("Original rushes"),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	// A missing display name cannot erase evidence of a different owning bin.
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QString(),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestAvbMetadata::incomplete_bins_cannot_supply_metadata()
{
	MediaTableModel model;
	model.setMediaFiles({row()});
	AvbBin unsupported = bin();
	unsupported.complete = false;
	AvbBin invalid = bin();
	invalid.valid = false;
	model.setAvbBins({unsupported, invalid});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestAvbMetadata::rescans_and_removals_preserve_provenance()
{
	MediaTableModel model;
	model.setAvbBins({bin()});
	model.setMediaFiles({row(), row(QStringLiteral("/media/new.mxf"))});
	QCOMPARE(model.fileAt(1).originalBin, QStringLiteral("Original rushes"));
	model.removeFilesByPath({QStringLiteral("/media/clip.mxf")});
	model.setAvbBins({});
	QCOMPARE(model.rowCount(), 1);
	QVERIFY(model.fileAt(0).originalBin.isEmpty());

	model.setAvbBins({bin()});
	MediaFile refreshed = row();
	refreshed.clipName = QStringLiteral("Database clip");
	refreshed.clipNameSource = MediaFile::ClipNameSource::Mdb;
	refreshed.originalBin = QStringLiteral("Database bin");
	model.setMediaFiles({refreshed});
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, refreshed.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(model.fileAt(0).originalBin, refreshed.originalBin);
}

QTEST_APPLESS_MAIN(TestAvbMetadata)
#include "tst_avbmetadata.moc"
