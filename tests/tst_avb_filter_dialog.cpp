// Exercise the asynchronous bin picker, ordered filter signal and table proxy.

#include "ui/dialogs/avb_filter_dialog.h"
#include "ui/media_filter_proxy.h"
#include "ui/media_table_model.h"
#include "media/mob_id_text.h"
#include "test_avb.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

// MARK: - Test helpers

namespace
{
	class TestableBinFilterDialog : public AvbFilterDialog
	{
	public:
		using AvbFilterDialog::dragEnterEvent;
		using AvbFilterDialog::dragMoveEvent;
		using AvbFilterDialog::dropEvent;
	};

	QByteArray partialBin()
	{
		TestAvb::Document d;
		d.objects = {{"ABIN", TestAvb::bin(false, {2})},
					 {"CMPO", TestAvb::composition(false, TestAvb::Master, "Unknown dependency", 0, {3})},
					 {"ZZZZ", QByteArray::fromHex("020103")}};
		return d.bytes();
	}

	struct Harness
	{
		MediaTableModel model;
		MediaFilterProxy proxy;
		TestableBinFilterDialog dialog;
		QSignalSpy errors;
		AvbFilter filter;
		QStringList names;

		Harness() : errors(&dialog, &AvbFilterDialog::loadError)
		{
			MediaFile hit;
			hit.mediaFilePath = QStringLiteral("/media/hit.mxf");
			hit.fileName = QStringLiteral("hit.mxf");
			hit.fileMobId = MobId::format(TestAvb::Source);
			hit.masterMobId = MobId::format(TestAvb::Master);
			MediaFile outside;
			outside.mediaFilePath = QStringLiteral("/media/outside.mxf");
			outside.fileName = QStringLiteral("outside.mxf");
			outside.fileMobId = MobId::format(TestAvb::Other);
			outside.masterMobId = hit.masterMobId; // A shared master must not admit this file.
			model.setMediaFiles({hit, outside});
			proxy.setSourceModel(&model);
			QObject::connect(&dialog, &AvbFilterDialog::filterChainChanged, &proxy,
							 [this](const AvbFilter &current, const QStringList &binNames)
							 {
								 filter = current;
								 names = binNames;
								 proxy.setBinFilter(current);
							 });
		}

		QListWidget *list() const { return dialog.findChild<QListWidget *>(QStringLiteral("BinList")); }
		QMessageBox *errorDialog() const
		{
			return dialog.findChild<QMessageBox *>();
		}
		void selectOnly(int index)
		{
			for (int row = 0; row < list()->count(); ++row)
				list()->item(row)->setCheckState(row == index ? Qt::Checked : Qt::Unchecked);
		}
		bool invoke(const char *slot) { return QMetaObject::invokeMethod(&dialog, slot, Qt::DirectConnection); }
	};
}

// MARK: - Test fixture

class TestAvbFilterDialog : public QObject
{
	Q_OBJECT
private slots:
	void loading_completion_auto_intersects();
	void empty_locator_selection_leaves_filter_unchanged();
	void partial_results_keep_warnings_and_references();
	void readable_bin_without_matching_files_filters_to_zero();
	void filter_buttons_build_ordered_chain();
	void snapshots_survive_reticking_and_bin_removal();
	void removing_multiple_bins_preserves_survivor_ticks_and_snapshots();
	void selection_snapshot_keeps_counts_names_and_ids_together();
	void bad_header_batch_is_parsed_asynchronously_and_reported_once();
	void non_avb_extension_is_rejected_without_parsing();
	void mixed_batch_reports_errors_once_and_keeps_usable_bins();
	void rejected_path_can_be_repaired_and_retried();
	void rejected_bin_does_not_reapply_a_cleared_filter();
	void drag_requires_avb_extension_and_recognizable_content_data();
	void drag_requires_avb_extension_and_recognizable_content();
	void rejected_drag_cannot_drop_renamed_text();
	void repeated_drag_paths_and_moves_do_not_repeat_errors();
	void mixed_drop_loads_only_recognizable_avb_files();
	void changed_content_is_rechecked_when_dropped();
	void removed_pending_reads_cannot_replace_retained_rows();
	void clear_while_loading_suppresses_automatic_filter();
};

// MARK: - Bin loading and membership

void TestAvbFilterDialog::loading_completion_auto_intersects()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Master.AVB"), TestAvb::mediaBin());
	h.dialog.addBinFromFile(path);
	QCOMPARE(h.list()->count(), 1); // row appears before background parsing completes
	QVERIFY(!(h.list()->item(0)->flags() & Qt::ItemIsUserCheckable));
	h.dialog.addBinFromFile(tmp.filePath("./Master.AVB")); // canonical duplicate while loading
	QCOMPARE(h.list()->count(), 1);
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.filter.steps.size(), 1);
	QCOMPARE(h.names, QStringList{QStringLiteral("Master")});
	QVERIFY(h.list()->item(0)->flags() & Qt::ItemIsUserCheckable);
	QCOMPARE(h.list()->item(0)->checkState(), Qt::Checked);
	h.dialog.addBinFromFile(path); // duplicate after parsing
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.errors.count(), 0);
}

void TestAvbFilterDialog::empty_locator_selection_leaves_filter_unchanged()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	// A master-only bin is valid but has no MSML file references.
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Empty.avb"), TestAvb::masterBin()));
	QTRY_COMPARE(loaded.count(), 1);
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	for (const auto slot : {"onIntersectClicked", "onSubtractClicked", "onAddClicked"})
	{
		QVERIFY(h.invoke(slot));
		QVERIFY(!h.filter.isActive());
		QCOMPARE(h.proxy.rowCount(), 2);
	}
	QCOMPARE(h.errors.count(), 0);
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::partial_results_keep_warnings_and_references()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	QSignalSpy warnings(&h.dialog, &AvbFilterDialog::loadWarning);
	auto originalRows = h.model.allFiles();
	for (auto &file : originalRows)
		file.operationScanReceipt.emplace().headerIdentity.fileMobId = file.fileMobId;
	h.model.setMediaFiles(originalRows);
	const auto unchangedRows = [&]
	{
		QCOMPARE(h.model.rowCount(), originalRows.size());
		for (int row = 0; row < h.model.rowCount(); ++row)
		{
			const auto &file = h.model.fileAt(row);
			QCOMPARE(file.clipName, originalRows[row].clipName);
			QCOMPARE(file.originalBin, originalRows[row].originalBin);
			QCOMPARE(file.effect, originalRows[row].effect);
			QCOMPARE(file.effectCategory, originalRows[row].effectCategory);
			QCOMPARE(file.fileMobId, originalRows[row].fileMobId);
			QCOMPARE(file.masterMobId, originalRows[row].masterMobId);
			QVERIFY(file.operationScanReceipt);
			QCOMPARE(file.operationScanReceipt->headerIdentity.fileMobId,
					 originalRows[row].operationScanReceipt->headerIdentity.fileMobId);
		}
	};
	// Loading another bin must not replace the existing operand.
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Good.avb"), TestAvb::mediaBin()));
	QTRY_VERIFY(h.filter.isActive());
	const auto previous = h.filter;
	TestAvb::Document document;
	document.objects = {{"ABIN", {}}, {"ZZZZ", QByteArray::fromHex("020103")}};
	const auto listed = TestAvb::appendFileSource(document, TestAvb::mediaLocator(false, TestAvb::Other), TestAvb::Other);
	document.objects[0].payload = TestAvb::bin(false, {listed});
	const auto problemPath = TestAvb::write(tmp.filePath("Problem.avb"), document.bytes());
	h.dialog.addBinFromFile(problemPath);
	QTRY_COMPARE(loaded.count(), 2);
	QCOMPARE(warnings.count(), 1);
	QCOMPARE(warnings.first().at(0).toString(), problemPath);
	unchangedRows();
	QCOMPARE(h.errors.count(), 0);
	QCOMPARE(h.list()->count(), 2);
	QVERIFY(h.list()->item(1)->text().contains(QStringLiteral("Results may be incomplete")));
	QVERIFY(h.filter.hasSameCriteria(previous)); // Loading never silently rewrites an existing chain.
	h.dialog.clearFilterSteps();
	h.selectOnly(1);
	QVERIFY(h.invoke("onAddClicked"));
	QCOMPARE(h.filter.steps.size(), 1);
	QVERIFY(h.filter.resultsMayBeIncomplete());
	QCOMPARE(h.filter.steps[0].fileReferences.fullIds, QSet<QString>{MobId::format(TestAvb::Other)});
	QVERIFY(h.filter.steps[0].fileReferences.legacyKeys.isEmpty());
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 1);
	const auto applied = h.filter;
	const auto warningSnapshot = h.filter.steps[0].warnings;
	h.list()->clearSelection();
	h.list()->item(1)->setSelected(true);
	QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
	QVERIFY(h.filter.hasSameCriteria(applied));
	QCOMPARE(h.filter.steps[0].warnings, warningSnapshot);
	unchangedRows();
	QVERIFY(h.dialog.findChild<QLabel *>(QStringLiteral("BinChainSummary"))->text().contains(QStringLiteral("Results may be incomplete")));
	h.dialog.clearFilterSteps();
	QVERIFY(!h.filter.resultsMayBeIncomplete());
}

void TestAvbFilterDialog::readable_bin_without_matching_files_filters_to_zero()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	// This full file ID is absent from both media rows.
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Unmatched.avb"), TestAvb::mediaBin({TestAvb::Master})));
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 0);
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.errors.count(), 0);
}

// MARK: - Filter chains and selection snapshots

void TestAvbFilterDialog::filter_buttons_build_ordered_chain()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("First.avb"), TestAvb::mediaBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Second.avb"), TestAvb::mediaBin({TestAvb::Source, TestAvb::Other})));
	QCOMPARE(loaded.count(), 0);
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	const QStringList binNames{QStringLiteral("First"), QStringLiteral("Second")};
	QCOMPARE(h.names, binNames);
	QCOMPARE(h.filter.steps.size(), 1);
	QCOMPARE(h.proxy.rowCount(), 2);

	const auto intersect = h.dialog.findChild<QPushButton *>(QStringLiteral("BinIntersectButton"));
	const auto subtract = h.dialog.findChild<QPushButton *>(QStringLiteral("BinSubtractButton"));
	const auto add = h.dialog.findChild<QPushButton *>(QStringLiteral("BinAddButton"));
	QVERIFY(intersect && subtract && add);
	h.dialog.clearFilterSteps();
	h.selectOnly(0);
	intersect->click();
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 0);
	h.selectOnly(1);
	subtract->click();
	QCOMPARE(h.proxy.rowCount(), 0);
	h.selectOnly(0);
	add->click();
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 0);
	QCOMPARE(h.filter.steps.size(), 3);
	QCOMPARE(h.filter.steps[0].operation, AvbFilter::Operation::Intersect);
	QCOMPARE(h.filter.steps[1].operation, AvbFilter::Operation::Subtract);
	QCOMPARE(h.filter.steps[2].operation, AvbFilter::Operation::Add);
	QCOMPARE(h.names, binNames);
	QCOMPARE(h.errors.count(), 0);
}

void TestAvbFilterDialog::removing_multiple_bins_preserves_survivor_ticks_and_snapshots()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	for (int i = 0; i < 4; ++i)
		h.dialog.addBinFromFile(TestAvb::write(tmp.filePath(QStringLiteral("Bin%1.avb").arg(i)),
											   TestAvb::mediaBin({i == 3 ? TestAvb::Other : TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 4);
	QTRY_VERIFY(h.filter.isActive());
	const AvbFilter previousFilter = h.filter;
	h.list()->item(1)->setCheckState(Qt::Unchecked);
	h.list()->item(0)->setSelected(true);
	h.list()->item(2)->setSelected(true);
	QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
	QCOMPARE(h.list()->count(), 2);
	QCOMPARE(h.list()->item(0)->text(), QStringLiteral("Bin1"));
	QCOMPARE(h.list()->item(0)->checkState(), Qt::Unchecked);
	QCOMPARE(h.list()->item(1)->text(), QStringLiteral("Bin3"));
	QCOMPARE(h.list()->item(1)->checkState(), Qt::Checked);
	QVERIFY(h.filter.hasSameCriteria(previousFilter));
	h.dialog.clearFilterSteps();
	QVERIFY(h.invoke("onIntersectClicked"));
	QCOMPARE(h.names, QStringList{QStringLiteral("Bin3")});
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 1);
}

void TestAvbFilterDialog::snapshots_survive_reticking_and_bin_removal()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::mediaBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("File.avb"), TestAvb::mediaBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearFilterSteps();
	h.selectOnly(0);
	QVERIFY(h.invoke("onIntersectClicked"));
	h.selectOnly(1);
	QCOMPARE(h.proxy.rowCount(), 1); // tick changes do not alter applied operands
	QVERIFY(h.invoke("onSubtractClicked"));
	QCOMPARE(h.proxy.rowCount(), 0);
	QVERIFY(QMetaObject::invokeMethod(&h.dialog, "onRemoveStep", Qt::DirectConnection, Q_ARG(int, 0)));
	QCOMPARE(h.proxy.rowCount(), 1); // leading Subtract now uses the media-row universe
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 1);
	for (int i = 0; i < 2; ++i)
	{
		h.list()->clearSelection();
		h.list()->item(0)->setSelected(true);
		QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
		QCOMPARE(h.proxy.rowCount(), 1);
		QCOMPARE(h.filter.steps.size(), 1);
	}
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.names, QStringList{QStringLiteral("File")});
	h.dialog.clearFilterSteps();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

void TestAvbFilterDialog::selection_snapshot_keeps_counts_names_and_ids_together()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("First.avb"), TestAvb::mediaBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Second.avb"), TestAvb::mediaBin({TestAvb::Other})));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Empty.avb"), TestAvb::masterBin({})));
	QTRY_COMPARE(loaded.count(), 3);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearFilterSteps();
	h.selectOnly(1);
	h.list()->item(2)->setCheckState(Qt::Checked);
	const auto summary = h.dialog.findChild<QLabel *>(QStringLiteral("BinListSummary"));
	QVERIFY(summary);
	QCOMPARE(summary->text(), QStringLiteral("3 loaded, 2 ticked"));
	QVERIFY(h.invoke("onIntersectClicked"));
	QCOMPARE(h.filter.steps.size(), 1);
	QCOMPARE(h.filter.steps.first().binDisplayNames,
			 (QVector<QString>{QStringLiteral("Second"), QStringLiteral("Empty")}));
	QCOMPARE(h.filter.steps.first().fileReferences.fullIds, QSet<QString>{MobId::format(TestAvb::Other)});
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.proxy.mapToSource(h.proxy.index(0, 0)).row(), 1);
	h.selectOnly(-1);
	QCOMPARE(summary->text(), QStringLiteral("3 loaded, 0 ticked"));
	QVERIFY(!h.dialog.findChild<QPushButton *>(QStringLiteral("BinIntersectButton"))->isEnabled());
	QVERIFY(h.invoke("onAddClicked"));
	QCOMPARE(h.filter.steps.size(), 1);
}

// MARK: - Diagnostics and retry

void TestAvbFilterDialog::bad_header_batch_is_parsed_asynchronously_and_reported_once()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	const QStringList paths{
		TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Ordinary text")),
		tmp.filePath("Missing.avb")};
	for (int index = 0; index < paths.size(); ++index)
	{
		h.dialog.addBinFromFile(paths.at(index));
		h.dialog.addBinFromFile(paths.at(index)); // Pending duplicates do not schedule another read.
		QCOMPARE(h.list()->count(), index + 1);
	}
	QCOMPARE(loaded.count(), 0);
	QCOMPARE(h.errors.count(), 0);
	QTRY_COMPARE(loaded.count(), paths.size());
	QCOMPARE(h.errors.count(), paths.size());
	QSet<QString> rejectedPaths;
	for (const auto &error : h.errors)
	{
		rejectedPaths.insert(error.at(0).toString());
		QVERIFY(!error.at(1).toString().isEmpty());
	}
	QCOMPARE(rejectedPaths, QSet<QString>(paths.cbegin(), paths.cend()));
	QCOMPARE(h.list()->count(), 0);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

void TestAvbFilterDialog::non_avb_extension_is_rejected_without_parsing()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Bin.txt"), TestAvb::mediaBin());
	h.dialog.addBinFromFile(path);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), path);
	QCOMPARE(h.errors.first().at(1).toString(),
			 QStringLiteral("Choose an Avid bin file with an .avb extension."));
	QVERIFY(!qvariant_cast<AvbBin>(loaded.first().first()).usable);
	QCoreApplication::processEvents();
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
}

void TestAvbFilterDialog::mixed_batch_reports_errors_once_and_keeps_usable_bins()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	QSignalSpy warnings(&h.dialog, &AvbFilterDialog::loadWarning);
	const auto damagedPath = TestAvb::write(tmp.filePath("Damaged.avb"), TestAvb::mediaBin().left(30));
	const auto partialPath = TestAvb::write(tmp.filePath("Partial.avb"), partialBin());
	h.dialog.addBinFromFile(damagedPath);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::mediaBin()));
	h.dialog.addBinFromFile(partialPath);
	QTRY_COMPARE(loaded.count(), 3);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), damagedPath);
	QCOMPARE(warnings.count(), 1);
	QCOMPARE(warnings.first().at(0).toString(), partialPath);
	QCOMPARE(h.list()->count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.names, (QStringList{QStringLiteral("Usable"), QStringLiteral("Partial")}));
	QVERIFY(h.filter.resultsMayBeIncomplete());
	QCOMPARE(h.proxy.rowCount(), 1);
	int usable = 0;
	for (const auto &attempt : loaded)
		usable += qvariant_cast<AvbBin>(attempt.first()).isUsable();
	QCOMPARE(usable, 2);
	QCoreApplication::processEvents();
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::rejected_path_can_be_repaired_and_retried()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Repair.avb"), TestAvb::mediaBin().left(30));
	h.dialog.addBinFromFile(path);
	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.list()->count(), 0);
	QVERIFY(!h.errorDialog());
	QCOMPARE(TestAvb::write(path, TestAvb::mediaBin()), path);
	h.dialog.addBinFromFile(path);
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::rejected_bin_does_not_reapply_a_cleared_filter()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::mediaBin()));
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearFilterSteps();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Rejected.avb"), QByteArray("Ordinary text")));
	QTRY_COMPARE(loaded.count(), 2);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.list()->item(0)->checkState(), Qt::Checked);
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

// MARK: - Drag and drop validation

void TestAvbFilterDialog::drag_requires_avb_extension_and_recognizable_content_data()
{
	QTest::addColumn<QString>("name");
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("accepted");
	QTest::newRow("renamed-text") << QStringLiteral("Notes.avb") << QByteArray("Notes") << false;
	QTest::newRow("actual-bin-wrong-extension") << QStringLiteral("Bin.txt") << TestAvb::mediaBin() << false;
	QTest::newRow("uppercase-extension") << QStringLiteral("Bin.AVB") << TestAvb::mediaBin() << true;
	QTest::newRow("recognizable-damaged-bin") << QStringLiteral("Bin.avb") << TestAvb::mediaBin().left(30) << true;
}

void TestAvbFilterDialog::drag_requires_avb_extension_and_recognizable_content()
{
	QFETCH(QString, name);
	QFETCH(QByteArray, bytes);
	QFETCH(bool, accepted);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QMimeData mime;
	const auto path = TestAvb::write(tmp.filePath(name), bytes);
	mime.setUrls({QUrl::fromLocalFile(path)});
	QDragEnterEvent event(QPoint{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dragEnterEvent(&event);
	QCOMPARE(event.isAccepted(), accepted);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), accepted ? 0 : 1);
	if (!accepted)
	{
		QCOMPARE(h.errors.first().at(0).toString(), path);
		const auto expectedReason = name.endsWith(QStringLiteral(".avb"), Qt::CaseInsensitive)
										? QStringLiteral("This file is not an Avid bin.")
										: QStringLiteral("Choose an Avid bin file with an .avb extension.");
		QCOMPARE(h.errors.first().at(1).toString(), expectedReason);
	}
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::rejected_drag_cannot_drop_renamed_text()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	QMimeData mime;
	mime.setUrls({QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Notes")))});
	QDragEnterEvent enter(QPoint{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dragEnterEvent(&enter);
	QVERIFY(!enter.isAccepted());
	QCOMPARE(h.errors.count(), 1);
	// The platform ordinarily omits drop after a rejected enter. Delivering it
	// explicitly verifies that the drop handler also enforces the gate.
	QDropEvent drop(QPointF{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dropEvent(&drop);
	QVERIFY(!drop.isAccepted());
	QCoreApplication::processEvents();
	QCOMPARE(loaded.count(), 0);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
}

void TestAvbFilterDialog::repeated_drag_paths_and_moves_do_not_repeat_errors()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	const auto rejected = QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Notes")));
	const auto accepted = QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::mediaBin()));
	QMimeData mime;
	mime.setUrls({rejected, accepted, rejected, accepted});
	QDragEnterEvent enter(QPoint{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dragEnterEvent(&enter);
	QVERIFY(enter.isAccepted());
	QCOMPARE(h.errors.count(), 1);
	for (int position = 0; position < 4; ++position)
	{
		QDragMoveEvent move(QPoint(position, position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		h.dialog.dragMoveEvent(&move);
		QVERIFY(move.isAccepted());
		QCOMPARE(h.errors.count(), 1);
	}
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	QDropEvent drop(QPointF{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dropEvent(&drop);
	QVERIFY(drop.isAccepted());
	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::mixed_drop_loads_only_recognizable_avb_files()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	QMimeData mime;
	mime.setUrls({QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::mediaBin())),
				  QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Notes"))),
				  QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Other.txt"), TestAvb::mediaBin({TestAvb::Source})))});
	QDragEnterEvent enter(QPoint{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dragEnterEvent(&enter);
	QVERIFY(enter.isAccepted());
	QDropEvent drop(QPointF{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCOMPARE(h.errors.count(), 2);
	h.dialog.dropEvent(&drop);
	QVERIFY(drop.isAccepted());
	QCOMPARE(h.list()->count(), 1);
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.names, QStringList{QStringLiteral("Usable")});
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(qvariant_cast<AvbBin>(loaded.first().first()).displayName, QStringLiteral("Usable"));
	QCOMPARE(h.errors.count(), 2);
	QVERIFY(!h.errorDialog());
}

void TestAvbFilterDialog::changed_content_is_rechecked_when_dropped()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Changed.avb"), TestAvb::mediaBin());
	QMimeData mime;
	mime.setUrls({QUrl::fromLocalFile(path)});
	QDragEnterEvent enter(QPoint{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dragEnterEvent(&enter);
	QVERIFY(enter.isAccepted());
	QCOMPARE(h.errors.count(), 0);
	QCOMPARE(TestAvb::write(path, QByteArray("Replaced by ordinary text")), path);
	QDropEvent drop(QPointF{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dropEvent(&drop);
	QVERIFY(drop.isAccepted());
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.errors.count(), 0);
	QTRY_COMPARE(loaded.count(), 1);
	QVERIFY(!qvariant_cast<AvbBin>(loaded.first().first()).usable);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), path);
	QCOMPARE(h.errors.first().at(1).toString(),
			 QStringLiteral("Invalid AVB byte-order signature."));
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

// MARK: - Pending read lifecycle

void TestAvbFilterDialog::removed_pending_reads_cannot_replace_retained_rows()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	for (int i = 0; i < 8; ++i)
	{
		const auto bytes = i % 4 == 0	? partialBin()
						   : i % 4 == 1 ? TestAvb::mediaBin().left(30)
						   : i % 4 == 2 ? QByteArray("Ordinary text")
										: TestAvb::mediaBin({TestAvb::Master});
		h.dialog.addBinFromFile(TestAvb::write(tmp.filePath(QString("Remove%1.avb").arg(i)),
											   bytes));
		h.list()->item(0)->setSelected(true);
		QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
		QCOMPARE(h.list()->count(), 0);
	}
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Retained.avb"), TestAvb::mediaBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCoreApplication::processEvents();
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 0);
	QVERIFY(!h.errorDialog());
	QCOMPARE(h.names, QStringList{QStringLiteral("Retained")});
	QCOMPARE(h.proxy.rowCount(), 1);
	QVERIFY(h.filter.steps.first().fileReferences.fullIds.contains(MobId::format(TestAvb::Source)));
	QVERIFY(!h.filter.steps.first().fileReferences.fullIds.contains(MobId::format(TestAvb::Master)));
	QVERIFY(h.filter.steps.first().fileReferences.legacyKeys.isEmpty());
}

void TestAvbFilterDialog::clear_while_loading_suppresses_automatic_filter()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &AvbFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::mediaBin()));
	h.dialog.clearFilterSteps();
	QTRY_COMPARE(loaded.count(), 1);
	QCoreApplication::processEvents();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	QVERIFY(h.invoke("onIntersectClicked"));
	QVERIFY(h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 1);
}

QTEST_MAIN(TestAvbFilterDialog)
#include "tst_avb_filter_dialog.moc"
