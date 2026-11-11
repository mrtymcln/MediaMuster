#include "avb/avb_bin_loader.h"
#include "media/mob_id_text.h"
#include "test_avb.h"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class TestAvbInventoryStress final : public QObject
{
	Q_OBJECT
private slots:
	void large_object_inventory_has_no_policy_limit();
};

void TestAvbInventoryStress::large_object_inventory_has_no_policy_limit()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	constexpr quint32 opaqueObjectCount = 2'097'152;
	constexpr quint32 sourceHandle = opaqueObjectCount + 2;
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false, {sourceHandle})},
						{"CMPO", TestAvb::composition(false, TestAvb::Source, "File source", 0, {}, 0, true, sourceHandle + 1, 3)},
						{"MDFL", TestAvb::fileDescriptor(false, sourceHandle + 2)},
						{"MSML", TestAvb::mediaLocator(false)}};
	auto bytes = document.bytes();
	TestAvb::replaceU32(bytes, document.countOffset, opaqueObjectCount + quint32(document.objects.size()));
	const auto fileSourceOffset = document.chunkOffsets[1];
	const auto fileSourceChunks = bytes.sliced(fileSourceOffset);
	TestAvb::Bytes opaqueChunk(false);
	opaqueChunk.fourcc("ZZZZ");
	opaqueChunk.u32(1);
	opaqueChunk.u8(3);
	QFile file(tmp.filePath("many-objects.avb"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(bytes.first(fileSourceOffset)), fileSourceOffset);
	QCOMPARE(file.write(opaqueChunk.data.repeated(opaqueObjectCount)), qint64(opaqueObjectCount) * opaqueChunk.data.size());
	// Resolving the listed file requires reading through the entire inventory.
	QCOMPARE(file.write(fileSourceChunks), fileSourceChunks.size());
	file.close();
	const auto result = AvbBinLoader::load(file.fileName());
	QVERIFY2(result.usable, qPrintable(result.error));
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.warnings.isEmpty());
	QCOMPARE(result.mediaFileIds.fullIds, QSet<QString>{MobId::format(TestAvb::Source)});
	QVERIFY(result.mediaFileIds.legacyKeys.isEmpty());
}

QTEST_APPLESS_MAIN(TestAvbInventoryStress)
#include "tst_avb_inventory_stress.moc"
