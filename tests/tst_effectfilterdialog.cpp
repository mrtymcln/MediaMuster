#include "effectfilterdialog.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

namespace
{
	MediaFile render(const QString &effect, const QString &volume, const QString &name)
	{
		MediaFile row;
		row.type = MediaFile::Type::Precompute;
		row.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
		row.effect = effect;
		row.effectCategory = QStringLiteral("Blend");
		row.volumeName = QStringLiteral("EDIT");
		row.volumePath = volume;
		row.fileName = name;
		row.filePath = volume + QLatin1Char('/') + name;
		return row;
	}

	QVector<MediaFile> rows()
	{
		QVector<MediaFile> files{
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("warp1.mxf")),
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("warp2.mxf")),
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT2"), QStringLiteral("warp3.mxf")),
			render(QStringLiteral("Dissolve"), QStringLiteral("/Volumes/EDIT2"), QStringLiteral("dissolve.mxf"))};
		auto resize = render(QStringLiteral("Resize"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("resize.mxf"));
		resize.effectCategory = QStringLiteral("Image");
		files.append(resize);
		auto title = render(QStringLiteral("Title"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("title.mxf"));
		title.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
		title.effectCategory = QStringLiteral("Title");
		files.append(title);
		title.effect = QStringLiteral("3D Warp"); // same inferred name must not change its proven subtype
		title.fileName = QStringLiteral("renamed-title.mxf");
		title.filePath = QStringLiteral("/Volumes/EDIT/renamed-title.mxf");
		files.append(title);
		auto unknown = render(QStringLiteral("Renamed"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("unknown.mxf"));
		unknown.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
		unknown.effectCategory.clear();
		files.append(unknown);
		auto ordinary = render(QStringLiteral("Ordinary fake effect"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("source.mxf"));
		ordinary.type = MediaFile::Type::Media;
		files.append(ordinary);
		return files;
	}

	QTreeWidgetItem *choice(EffectFilterDialog &dialog, const QStringList &path)
	{
		auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("effectChoices"));
		if (!tree || tree->topLevelItemCount() != 1) return nullptr;
		auto *item = tree->topLevelItem(0);
		for (const auto &name : path)
		{
			QTreeWidgetItem *next = nullptr;
			for (int i = 0; i < item->childCount(); ++i)
				if (item->child(i)->text(0) == name) next = item->child(i);
			if (!next) return nullptr;
			item = next;
		}
		return item;
	}

	QString matchingText(EffectFilterDialog &dialog)
	{
		return dialog.findChild<QLabel *>(QStringLiteral("effectMatchingCount"))->text();
	}

	QStringList matchingFiles(const QVector<MediaFile> &files, const PrecomputeFilter &filter, const QString &volume = {})
	{
		MediaTableModel model;
		model.setMediaFiles(files);
		MediaFilterProxy proxy;
		proxy.setSourceModel(&model);
		proxy.setEffectDetailsEnabled(true);
		proxy.setPrecomputeTreeFilter(filter);
		proxy.setEffectVolumeFilter(volume);
		QStringList names;
		for (int i = 0; i < proxy.rowCount(); ++i)
			names.append(model.fileAt(proxy.mapToSource(proxy.index(i, 0)).row()).fileName);
		names.sort();
		return names;
	}
}

class TestEffectFilterDialog : public QObject
{
	Q_OBJECT
private slots:
	void initial_all_and_empty_are_distinct();
	void branches_combine_and_reopen_without_crossing_subtypes();
	void collapsed_branch_selects_all_descendants();
	void volume_changes_preserve_zero_match_choices();
	void unknown_effect_and_unknown_subtype_remain_distinct();
	void apply_cancel_and_keyboard_actions();
	void standard_qt_layout_and_preview();
};

void TestEffectFilterDialog::initial_all_and_empty_are_distinct()
{
	EffectFilterDialog dialog(rows(), {}, {});
	auto *root = choice(dialog, {});
	QVERIFY(root);
	QCOMPARE(root->childCount(), 3);
	QCOMPARE(root->checkState(0), Qt::Checked);
	QCOMPARE(matchingText(dialog), QStringLiteral("8 matching files"));
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter()).size(), 8);
	root->setCheckState(0, Qt::Unchecked);
	QVERIFY(dialog.precomputeFilter().active);
	QVERIFY(dialog.precomputeFilter().paths.isEmpty());
	QCOMPARE(matchingText(dialog), QStringLiteral("0 matching files"));
	QVERIFY(matchingFiles(rows(), dialog.precomputeFilter()).isEmpty());
	QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("applyEffectFilter"))->isEnabled());
	root->setCheckState(0, Qt::Checked);
	QCOMPARE(dialog.precomputeFilter().paths.size(), 1);
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter()).size(), 8);
}

void TestEffectFilterDialog::branches_combine_and_reopen_without_crossing_subtypes()
{
	EffectFilterDialog dialog(rows(), {true, {}}, {});
	auto *warp = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")});
	auto *titles = choice(dialog, {QStringLiteral("Titles and Matte Keys")});
	QVERIFY(warp && titles);
	warp->setCheckState(0, Qt::Checked);
	QCOMPARE(choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend")})->checkState(0), Qt::PartiallyChecked);
	QCOMPARE(choice(dialog, {QStringLiteral("Rendered Effects")})->checkState(0), Qt::PartiallyChecked);
	QCOMPARE(choice(dialog, {})->checkState(0), Qt::PartiallyChecked);
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter()).size(), 3);
	titles->setCheckState(0, Qt::Checked);
	const QStringList expected{QStringLiteral("renamed-title.mxf"), QStringLiteral("title.mxf"),
		QStringLiteral("warp1.mxf"), QStringLiteral("warp2.mxf"), QStringLiteral("warp3.mxf")};
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter()), expected);
	EffectFilterDialog reopened(rows(), dialog.precomputeFilter(), {});
	QCOMPARE(matchingFiles(rows(), reopened.precomputeFilter()), expected);
	QCOMPARE(choice(reopened, {QStringLiteral("Titles and Matte Keys")})->checkState(0), Qt::Checked);
	QCOMPARE(choice(reopened, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("Dissolve")})->checkState(0), Qt::Unchecked);
}

void TestEffectFilterDialog::collapsed_branch_selects_all_descendants()
{
	EffectFilterDialog dialog(rows(), {true, {}}, {});
	auto *resize = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Image"), QStringLiteral("Resize")});
	auto *blend = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend")});
	auto *warp = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")});
	auto *dissolve = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("Dissolve")});
	QVERIFY(resize && blend && warp && dissolve);
	resize->setCheckState(0, Qt::Checked);
	blend->setExpanded(false);
	QCOMPARE(matchingText(dialog), QStringLiteral("1 matching file"));
	blend->setCheckState(0, Qt::Checked);
	QCOMPARE(warp->checkState(0), Qt::Checked);
	QCOMPARE(dissolve->checkState(0), Qt::Checked);
	QCOMPARE(resize->checkState(0), Qt::Checked);
	QCOMPARE(matchingText(dialog), QStringLiteral("5 matching files"));
	QVERIFY(!blend->isExpanded());
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter()).size(), 5);
	blend->setCheckState(0, Qt::Unchecked);
	QCOMPARE(warp->checkState(0), Qt::Unchecked);
	QCOMPARE(dissolve->checkState(0), Qt::Unchecked);
	QCOMPARE(resize->checkState(0), Qt::Checked);
	QCOMPARE(matchingText(dialog), QStringLiteral("1 matching file"));
}

void TestEffectFilterDialog::volume_changes_preserve_zero_match_choices()
{
	EffectFilterDialog dialog(rows(), {true, {}}, {});
	auto *volumes = dialog.findChild<QComboBox *>(QStringLiteral("effectVolume"));
	auto *dissolve = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("Dissolve")});
	QVERIFY(volumes && dissolve);
	QCOMPARE(volumes->count(), 3);
	QVERIFY(volumes->itemText(1) != volumes->itemText(2));
	dissolve->setCheckState(0, Qt::Checked);
	volumes->setCurrentIndex(volumes->findData(QStringLiteral("/Volumes/EDIT")));
	QCOMPARE(dissolve->checkState(0), Qt::Checked);
	QCOMPARE(matchingText(dialog), QStringLiteral("0 matching files"));
	QVERIFY(matchingFiles(rows(), dialog.precomputeFilter(), dialog.selectedVolume()).isEmpty());
	volumes->setCurrentIndex(volumes->findData(QStringLiteral("/Volumes/EDIT2")));
	QCOMPARE(matchingText(dialog), QStringLiteral("1 matching file"));
	QCOMPARE(matchingFiles(rows(), dialog.precomputeFilter(), dialog.selectedVolume()), QStringList{QStringLiteral("dissolve.mxf")});
	EffectFilterDialog disconnected(rows(), dialog.precomputeFilter(), QStringLiteral("/Volumes/Disconnected"));
	QCOMPARE(disconnected.selectedVolume(), QStringLiteral("/Volumes/Disconnected"));
	QCOMPARE(matchingText(disconnected), QStringLiteral("0 matching files"));
}

void TestEffectFilterDialog::unknown_effect_and_unknown_subtype_remain_distinct()
{
	auto files = rows();
	auto unknownEffect = render({}, QStringLiteral("/Volumes/EDIT"), QStringLiteral("renamed-render.mxf"));
	unknownEffect.effectCategory.clear();
	files.append(unknownEffect);
	EffectFilterDialog dialog(files, {true, {}}, {});
	auto *renderUnknown = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("unknown"), QStringLiteral("unknown")});
	auto *subtypeUnknown = choice(dialog, {QStringLiteral("unknown")});
	QVERIFY(renderUnknown && subtypeUnknown);
	renderUnknown->setCheckState(0, Qt::Checked);
	QCOMPARE(subtypeUnknown->checkState(0), Qt::Unchecked);
	QCOMPARE(matchingFiles(files, dialog.precomputeFilter()), QStringList{QStringLiteral("renamed-render.mxf")});
	subtypeUnknown->setCheckState(0, Qt::Checked);
	QCOMPARE(matchingFiles(files, dialog.precomputeFilter()).size(), 2);
}

void TestEffectFilterDialog::apply_cancel_and_keyboard_actions()
{
	PrecomputeFilter initial{true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")}}};
	EffectFilterDialog dialog(rows(), initial, {});
	QSignalSpy accepted(&dialog, &QDialog::accepted);
	QSignalSpy rejected(&dialog, &QDialog::rejected);
	auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("effectChoices"));
	auto *warp = choice(dialog, {QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")});
	dialog.show();
	tree->setCurrentItem(warp);
	tree->setFocus();
	QTest::keyClick(tree, Qt::Key_Space);
	QCOMPARE(warp->checkState(0), Qt::Unchecked);
	QTest::keyClick(tree, Qt::Key_Space);
	QCOMPARE(warp->checkState(0), Qt::Checked);
	QTest::keyClick(&dialog, Qt::Key_Escape);
	QCOMPARE(rejected.size(), 1);
	QCOMPARE(accepted.size(), 0);
	QCOMPARE(initial.paths.size(), 1); // the caller's applied filter stays unchanged

	EffectFilterDialog apply(rows(), initial, {});
	apply.show();
	QTest::keyClick(&apply, Qt::Key_Return);
	QCOMPARE(apply.result(), int(QDialog::Accepted));
	QCOMPARE(matchingFiles(rows(), apply.precomputeFilter()).size(), 3);
}

void TestEffectFilterDialog::standard_qt_layout_and_preview()
{
	EffectFilterDialog dialog(rows(), {true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")}}}, {});
	dialog.show();
	QTest::qWait(20);
	auto *volumes = dialog.findChild<QComboBox *>(QStringLiteral("effectVolume"));
	auto *tree = dialog.findChild<QTreeWidget *>(QStringLiteral("effectChoices"));
	QVERIFY(volumes && tree);
	QVERIFY(dialog.findChildren<QLineEdit *>().isEmpty());
	QVERIFY(volumes->geometry().bottom() < tree->geometry().top());
	QCOMPARE(volumes->minimumWidth(), 220);
	QCOMPARE(dialog.width(), 430);
	QCOMPARE(dialog.minimumWidth(), 380);
	QVERIFY(tree->isHeaderHidden());
	QCOMPARE(tree->columnCount(), 1);
	QCOMPARE(dialog.findChildren<QPushButton *>().size(), 2);
	QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("effectFilterSummary")));
	QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("effectFilterDimension")));
	QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("clearEffectFilter")));
	QCOMPARE(dialog.findChild<QPushButton *>(QStringLiteral("applyEffectFilter"))->text(), QStringLiteral("Apply"));
	QVERIFY(!dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
	QCOMPARE(dialog.style(), QApplication::style());
	QCOMPARE(dialog.font(), QApplication::font());
	QCOMPARE(dialog.palette(), QApplication::palette());
	QCOMPARE(volumes->style(), QApplication::style());
	QVERIFY(dialog.styleSheet().isEmpty());
	const QString previewDir = qEnvironmentVariable("MEDIAMUSTER_QT_DIALOG_PREVIEW");
	if (!previewDir.isEmpty())
	{
		QVERIFY(QDir().mkpath(previewDir));
		QVERIFY(dialog.grab().save(previewDir + QStringLiteral("/qt-dialog.png")));
		dialog.resize(dialog.minimumSize());
		QTest::qWait(20);
		QVERIFY(dialog.grab().save(previewDir + QStringLiteral("/qt-dialog-minimum.png")));
		volumes->showPopup();
		QTest::qWait(20);
		QVERIFY(volumes->view()->window()->grab().save(previewDir + QStringLiteral("/qt-volume-popup.png")));
		volumes->hidePopup();
		auto manyRows = rows();
		for (int i = 0; i < 40; ++i)
			manyRows.append(render(QStringLiteral("Example effect %1").arg(i, 2, 10, QLatin1Char('0')),
				QStringLiteral("/Volumes/EDIT"), QStringLiteral("example%1.mxf").arg(i)));
		EffectFilterDialog scrolling(manyRows, {true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")}}}, {});
		scrolling.show();
		QTest::qWait(20);
		QVERIFY(scrolling.grab().save(previewDir + QStringLiteral("/qt-scrollbar.png")));
	}
}

QTEST_MAIN(TestEffectFilterDialog)
#include "tst_effectfilterdialog.moc"
