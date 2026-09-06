// Drive the actual asynchronous bin picker, ordered expression signal and
// table proxy. Fixtures are complete structured AVBs from testavb.h.

#include "binfilterdialog.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"
#include "mobid.h"
#include "testavb.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace
{
	class TestableBinFilterDialog : public BinFilterDialog
	{
	public:
		using BinFilterDialog::dragEnterEvent;
		using BinFilterDialog::dragMoveEvent;
		using BinFilterDialog::dropEvent;
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
		BinFilter filter;
		QStringList names;

		Harness() : errors(&dialog, &BinFilterDialog::loadError)
		{
			MediaFile hit;
			hit.filePath = QStringLiteral("/media/hit.mxf");
			hit.fileName = QStringLiteral("hit.mxf");
			hit.mobId = MobId::format(TestAvb::Source);
			hit.masterMobId = MobId::format(TestAvb::Master);
			MediaFile outside;
			outside.filePath = QStringLiteral("/media/outside.mxf");
			outside.fileName = QStringLiteral("outside.mxf");
			outside.mobId = MobId::format(TestAvb::Other);
			model.setMediaFiles({hit, outside});
			proxy.setSourceModel(&model);
			QObject::connect(&dialog, &BinFilterDialog::filterChainChanged, &proxy,
							 [this](const BinFilter &current, const QStringList &binNames)
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

class TestBinFilterDialog : public QObject
{
	Q_OBJECT
private slots:
	void loading_completion_auto_intersects();
	void loaded_bin_metadata_is_published_as_one_batch();
	void intersect_empty_bin_is_active_and_matches_nothing();
	void intersection_and_subtraction_use_row_membership_data();
	void intersection_and_subtraction_use_row_membership();
	void add_restores_a_previously_subtracted_row();
	void snapshots_survive_reticking_and_bin_removal();
	void failed_and_partial_bins_emit_errors_without_dialogs_data();
	void failed_and_partial_bins_emit_errors_without_dialogs();
	void bad_header_batch_is_parsed_asynchronously_and_reported_once();
	void non_avb_extension_is_rejected_without_parsing();
	void mixed_batch_reports_errors_once_and_keeps_usable_bins();
	void rejected_path_can_be_repaired_and_retried();
	void rejected_bin_does_not_reapply_a_cleared_filter_data();
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

void TestBinFilterDialog::loading_completion_auto_intersects()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Master.AVB"), TestAvb::masterBin());
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

void TestBinFilterDialog::intersect_empty_bin_is_active_and_matches_nothing()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Empty.avb"), TestAvb::masterBin({})));
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.filter.steps.size(), 1);
	QVERIFY(h.filter.steps.first().mobIds.isEmpty());
	QCOMPARE(h.proxy.rowCount(), 0);
	QVERIFY(h.dialog.findChild<QPushButton *>(QStringLiteral("BinIntersectButton"))->isEnabled());
	h.dialog.clearChain();
	QCOMPARE(h.proxy.rowCount(), 2);
	QVERIFY(h.invoke("onIntersectClicked"));
	QVERIFY(h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 0);
	QCOMPARE(h.errors.count(), 0);
	QVERIFY(!h.errorDialog());
}

void TestBinFilterDialog::loaded_bin_metadata_is_published_as_one_batch()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	QSignalSpy published(&h.dialog, &BinFilterDialog::binsChanged);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::masterBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("File.avb"), TestAvb::masterBin({TestAvb::Source})));
	QCOMPARE(published.count(), 0);
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_COMPARE(published.count(), 1);
	const auto batch = qvariant_cast<QVector<AvbBin>>(published.first().first());
	QCOMPARE(batch.size(), 2);
	QCOMPARE(batch[0].displayName, QStringLiteral("Master"));
	QCOMPARE(batch[1].displayName, QStringLiteral("File"));
	h.list()->clearSelection();
	h.list()->item(0)->setSelected(true);
	QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
	QCOMPARE(published.count(), 2); // removal retracts provenance immediately
	const auto remaining = qvariant_cast<QVector<AvbBin>>(published.last().first());
	QCOMPARE(remaining.size(), 1);
	QCOMPARE(remaining.first().displayName, QStringLiteral("File"));
}

void TestBinFilterDialog::intersection_and_subtraction_use_row_membership_data()
{
	QTest::addColumn<QByteArray>("firstBin");
	QTest::addColumn<QByteArray>("slot");
	QTest::addColumn<int>("expected");
	QTest::newRow("intersect-master-then-file") << TestAvb::masterBin({TestAvb::Master})
												<< QByteArray("onIntersectClicked") << 1;
	QTest::newRow("subtract-file-from-master-and-file") << TestAvb::masterBin({TestAvb::Master, TestAvb::Source})
														<< QByteArray("onSubtractClicked") << 0;
}

void TestBinFilterDialog::intersection_and_subtraction_use_row_membership()
{
	QFETCH(QByteArray, firstBin);
	QFETCH(QByteArray, slot);
	QFETCH(int, expected);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("First.avb"), firstBin));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("File.avb"), TestAvb::masterBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearChain();
	h.selectOnly(0);
	QVERIFY(h.invoke("onIntersectClicked"));
	QCOMPARE(h.proxy.rowCount(), 1);
	h.selectOnly(1);
	QVERIFY(h.invoke(slot.constData()));
	QCOMPARE(h.proxy.rowCount(), expected);
	QCOMPARE(h.filter.steps.size(), 2);
}

void TestBinFilterDialog::add_restores_a_previously_subtracted_row()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::masterBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("File.avb"), TestAvb::masterBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearChain();
	h.selectOnly(0);
	QVERIFY(h.invoke("onIntersectClicked"));
	h.selectOnly(1);
	QVERIFY(h.invoke("onSubtractClicked"));
	QCOMPARE(h.proxy.rowCount(), 0);
	h.selectOnly(0);
	QVERIFY(h.invoke("onAddClicked"));
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.filter.steps.size(), 3);
	QCOMPARE(h.names, (QStringList{QStringLiteral("Master"), QStringLiteral("File")}));
}

void TestBinFilterDialog::snapshots_survive_reticking_and_bin_removal()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::masterBin()));
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("File.avb"), TestAvb::masterBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearChain();
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
	h.dialog.clearChain();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

void TestBinFilterDialog::failed_and_partial_bins_emit_errors_without_dialogs_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("valid");
	QTest::newRow("empty-file") << QByteArray{} << false;
	QTest::newRow("truncated-signature") << QByteArray::fromHex("0600446f6d61696e444a424f") << false;
	QTest::newRow("renamed-text") << QByteArray("Ordinary text named .avb") << false;
	QTest::newRow("damaged-body") << TestAvb::masterBin().chopped(1) << false;
	QTest::newRow("unsupported-dependency") << partialBin() << true;
}

void TestBinFilterDialog::failed_and_partial_bins_emit_errors_without_dialogs()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, valid);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Problem.avb"), bytes);
	h.dialog.addBinFromFile(path);
	QCOMPARE(h.list()->count(), 1); // All .avb content validation runs in the worker.
	QCOMPARE(loaded.count(), 0);
	QCOMPARE(h.errors.count(), 0);
	QTRY_COMPARE(loaded.count(), 1);
	const auto parsed = qvariant_cast<AvbBin>(loaded.first().first());
	QCOMPARE(parsed.valid, valid);
	QVERIFY(!parsed.complete);
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), path);
	const auto reason = h.errors.first().at(1).toString();
	if (parsed.valid)
	{
		QCOMPARE(reason, QStringLiteral("This bin contains data that MediaMuster does not yet support; ") + parsed.warnings.join(QStringLiteral("; ")));
		QVERIFY(reason.contains(QStringLiteral("ZZZZ")));
	}
	else
		QCOMPARE(reason, parsed.error);
	QCoreApplication::processEvents();
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.dialog.findChild<QPushButton *>(QStringLiteral("BinIntersectButton"))->isEnabled());
	QVERIFY(h.invoke("onIntersectClicked"));
	QVERIFY(!h.filter.isActive());
}

void TestBinFilterDialog::bad_header_batch_is_parsed_asynchronously_and_reported_once()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const QStringList paths{
		TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Ordinary text")),
		TestAvb::write(tmp.filePath("Truncated.avb"), QByteArray::fromHex("0600446f6d61696e444a424f")),
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

void TestBinFilterDialog::non_avb_extension_is_rejected_without_parsing()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Bin.txt"), TestAvb::masterBin());
	h.dialog.addBinFromFile(path);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), path);
	QCOMPARE(h.errors.first().at(1).toString(),
			 QStringLiteral("Choose an Avid bin file with an .avb extension."));
	QVERIFY(!qvariant_cast<AvbBin>(loaded.first().first()).valid);
	QCoreApplication::processEvents();
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
}

void TestBinFilterDialog::mixed_batch_reports_errors_once_and_keeps_usable_bins()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	QSignalSpy published(&h.dialog, &BinFilterDialog::binsChanged);
	const auto damagedPath = TestAvb::write(tmp.filePath("Damaged.avb"), TestAvb::masterBin().chopped(1));
	const auto partialPath = TestAvb::write(tmp.filePath("Partial.avb"), partialBin());
	h.dialog.addBinFromFile(damagedPath);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::masterBin()));
	h.dialog.addBinFromFile(partialPath);
	QTRY_COMPARE(loaded.count(), 3);
	QCOMPARE(h.errors.count(), 2);
	const QSet<QString> rejectedPaths{
		h.errors.at(0).at(0).toString(), h.errors.at(1).at(0).toString()};
	QCOMPARE(rejectedPaths, (QSet<QString>{damagedPath, partialPath}));
	QCOMPARE(h.list()->count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.names, QStringList{QStringLiteral("Usable")});
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(published.count(), 1);
	const auto bins = qvariant_cast<QVector<AvbBin>>(published.first().first());
	QCOMPARE(bins.size(), 1);
	QCOMPARE(bins.first().displayName, QStringLiteral("Usable"));
	QCoreApplication::processEvents();
	QCOMPARE(h.errors.count(), 2);
	QVERIFY(!h.errorDialog());
}

void TestBinFilterDialog::rejected_path_can_be_repaired_and_retried()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Repair.avb"), TestAvb::masterBin().chopped(1));
	h.dialog.addBinFromFile(path);
	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.list()->count(), 0);
	QVERIFY(!h.errorDialog());
	QCOMPARE(TestAvb::write(path, TestAvb::masterBin()), path);
	h.dialog.addBinFromFile(path);
	QTRY_COMPARE(loaded.count(), 2);
	QTRY_VERIFY(h.filter.isActive());
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.proxy.rowCount(), 1);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
}

void TestBinFilterDialog::rejected_bin_does_not_reapply_a_cleared_filter_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::newRow("bad-header") << QByteArray("Ordinary text");
	QTest::newRow("damaged-body") << TestAvb::masterBin().chopped(1);
	QTest::newRow("unsupported-dependency") << partialBin();
}

void TestBinFilterDialog::rejected_bin_does_not_reapply_a_cleared_filter()
{
	QFETCH(QByteArray, bytes);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::masterBin()));
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	h.dialog.clearChain();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Rejected.avb"), bytes));
	QTRY_COMPARE(loaded.count(), 2);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.list()->item(0)->checkState(), Qt::Checked);
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

void TestBinFilterDialog::drag_requires_avb_extension_and_recognizable_content_data()
{
	QTest::addColumn<QString>("name");
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("accepted");
	QTest::newRow("plain-text") << QStringLiteral("Notes.txt") << QByteArray("Notes") << false;
	QTest::newRow("renamed-text") << QStringLiteral("Notes.avb") << QByteArray("Notes") << false;
	QTest::newRow("actual-bin-wrong-extension") << QStringLiteral("Bin.txt") << TestAvb::masterBin() << false;
	QTest::newRow("little-endian-bin") << QStringLiteral("Bin.avb") << TestAvb::masterBin() << true;
	QTest::newRow("big-endian-bin") << QStringLiteral("Bin.avb") << TestAvb::masterBin({TestAvb::Master}, true) << true;
	QTest::newRow("uppercase-extension") << QStringLiteral("Bin.AVB") << TestAvb::masterBin() << true;
	QTest::newRow("recognizable-damaged-bin") << QStringLiteral("Bin.avb") << TestAvb::masterBin().chopped(1) << true;
}

void TestBinFilterDialog::drag_requires_avb_extension_and_recognizable_content()
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

void TestBinFilterDialog::rejected_drag_cannot_drop_renamed_text()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	QSignalSpy published(&h.dialog, &BinFilterDialog::binsChanged);
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
	QCOMPARE(published.count(), 0);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
}

void TestBinFilterDialog::repeated_drag_paths_and_moves_do_not_repeat_errors()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	const auto rejected = QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Notes")));
	const auto accepted = QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::masterBin()));
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
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	QDropEvent drop(QPointF{}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	h.dialog.dropEvent(&drop);
	QVERIFY(drop.isAccepted());
	QTRY_COMPARE(loaded.count(), 1);
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(h.errors.count(), 1);
	QVERIFY(!h.errorDialog());
}

void TestBinFilterDialog::mixed_drop_loads_only_recognizable_avb_files()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	QSignalSpy published(&h.dialog, &BinFilterDialog::binsChanged);
	QMimeData mime;
	mime.setUrls({QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Usable.avb"), TestAvb::masterBin())),
				  QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Notes.avb"), QByteArray("Notes"))),
				  QUrl::fromLocalFile(TestAvb::write(tmp.filePath("Other.txt"), TestAvb::masterBin({TestAvb::Source})))});
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
	QCOMPARE(published.count(), 1);
	const auto bins = qvariant_cast<QVector<AvbBin>>(published.first().first());
	QCOMPARE(bins.size(), 1);
	QCOMPARE(bins.first().displayName, QStringLiteral("Usable"));
	QCOMPARE(h.errors.count(), 2);
	QVERIFY(!h.errorDialog());
}

void TestBinFilterDialog::changed_content_is_rechecked_when_dropped()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	const auto path = TestAvb::write(tmp.filePath("Changed.avb"), TestAvb::masterBin());
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
	QVERIFY(!qvariant_cast<AvbBin>(loaded.first().first()).valid);
	QCOMPARE(h.list()->count(), 0);
	QCOMPARE(h.errors.count(), 1);
	QCOMPARE(h.errors.first().at(0).toString(), path);
	QCOMPARE(h.errors.first().at(1).toString(),
			 QStringLiteral("Not an Avid bin: invalid byte-order marker."));
	QVERIFY(!h.errorDialog());
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
}

void TestBinFilterDialog::removed_pending_reads_cannot_replace_retained_rows()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	for (int i = 0; i < 8; ++i)
	{
		const auto bytes = i % 4 == 0	? partialBin()
						   : i % 4 == 1 ? TestAvb::masterBin().chopped(1)
						   : i % 4 == 2 ? QByteArray("Ordinary text")
										: TestAvb::masterBin();
		h.dialog.addBinFromFile(TestAvb::write(tmp.filePath(QString("Remove%1.avb").arg(i)),
											   bytes));
		h.list()->item(0)->setSelected(true);
		QVERIFY(h.invoke("onRemoveSelectedBinsClicked"));
		QCOMPARE(h.list()->count(), 0);
	}
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Retained.avb"), TestAvb::masterBin({TestAvb::Source})));
	QTRY_COMPARE(loaded.count(), 1);
	QTRY_VERIFY(h.filter.isActive());
	QCoreApplication::processEvents();
	QCOMPARE(h.list()->count(), 1);
	QCOMPARE(loaded.count(), 1);
	QCOMPARE(h.errors.count(), 0);
	QVERIFY(!h.errorDialog());
	QCOMPARE(h.names, QStringList{QStringLiteral("Retained")});
	QCOMPARE(h.proxy.rowCount(), 1);
	QVERIFY(h.filter.steps.first().mobIds.contains(MobId::format(TestAvb::Source)));
	QVERIFY(!h.filter.steps.first().mobIds.contains(MobId::format(TestAvb::Master)));
}

void TestBinFilterDialog::clear_while_loading_suppresses_automatic_filter()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	Harness h;
	QSignalSpy loaded(&h.dialog, &BinFilterDialog::binLoaded);
	h.dialog.addBinFromFile(TestAvb::write(tmp.filePath("Master.avb"), TestAvb::masterBin()));
	h.dialog.clearChain();
	QTRY_COMPARE(loaded.count(), 1);
	QCoreApplication::processEvents();
	QVERIFY(!h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 2);
	QVERIFY(h.invoke("onIntersectClicked"));
	QVERIFY(h.filter.isActive());
	QCOMPARE(h.proxy.rowCount(), 1);
}

QTEST_MAIN(TestBinFilterDialog)
#include "tst_binfilterdialog.moc"
