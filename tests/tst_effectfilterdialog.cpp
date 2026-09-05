#include "effectfilterdialog.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

namespace
{
	MediaFile render(const QString &effect, const QString &volume, const QString &file)
	{
		MediaFile row;
		row.type = MediaFile::Type::Precompute;
		row.effect = effect;
		row.effectCategory = QStringLiteral("Blend");
		row.volumeName = QStringLiteral("EDIT"); // labels alone cannot identify a volume
		row.volumePath = volume;
		row.filePath = volume + QLatin1Char('/') + file;
		return row;
	}

	QVector<MediaFile> rows()
	{
		QVector<MediaFile> files{
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("warp1.mxf")),
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("warp2.mxf")),
			render(QStringLiteral("3D Warp"), QStringLiteral("/Volumes/EDIT2"), QStringLiteral("warp3.mxf")),
			render(QStringLiteral("Dissolve"), QStringLiteral("/Volumes/EDIT2"), QStringLiteral("dissolve.mxf"))};
		auto ordinary = render(QStringLiteral("Ordinary fake effect"), QStringLiteral("/Volumes/EDIT"), QStringLiteral("source.mxf"));
		ordinary.type = MediaFile::Type::Media;
		files.append(ordinary);
		return files;
	}
}

class TestEffectFilterDialog : public QObject
{
	Q_OBJECT
private slots:
	void choose_effect_and_volume_filters_actual_rows();
	void search_preserves_checked_names_and_volume_change_drops_unavailable_names();
	void clear_returns_no_hidden_filter();
};

void TestEffectFilterDialog::choose_effect_and_volume_filters_actual_rows()
{
	const auto files = rows();
	EffectFilterDialog dialog(files, {}, {});
	auto *volumes = dialog.findChild<QComboBox *>(QStringLiteral("effectVolume"));
	auto *choices = dialog.findChild<QTreeWidget *>(QStringLiteral("effectChoices"));
	QVERIFY(volumes);
	QVERIFY(choices);
	QCOMPARE(volumes->count(), 3);
	QVERIFY(volumes->itemText(1) != volumes->itemText(2));
	QCOMPARE(choices->topLevelItemCount(), 2); // fake effect on ordinary media is excluded
	QCOMPARE(choices->topLevelItem(0)->text(0), QStringLiteral("3D Warp"));
	QCOMPARE(choices->topLevelItem(0)->text(2), QStringLiteral("3"));
	volumes->setCurrentIndex(volumes->findData(QStringLiteral("/Volumes/EDIT")));
	QCOMPARE(choices->topLevelItemCount(), 1);
	QCOMPARE(choices->topLevelItem(0)->text(2), QStringLiteral("2"));
	choices->topLevelItem(0)->setCheckState(0, Qt::Checked);
	QCOMPARE(dialog.selectedEffects(), QStringList{QStringLiteral("3D Warp")});
	QCOMPARE(dialog.selectedVolume(), QStringLiteral("/Volumes/EDIT"));

	MediaTableModel model;
	model.setMediaFiles(files);
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setEffectDetailsEnabled(true);
	proxy.setEffectFilter(dialog.selectedEffects());
	proxy.setEffectVolumeFilter(dialog.selectedVolume());
	QCOMPARE(proxy.rowCount(), 2);
	proxy.setEffectDetailsEnabled(false);
	QCOMPARE(proxy.rowCount(), 5);
}

void TestEffectFilterDialog::search_preserves_checked_names_and_volume_change_drops_unavailable_names()
{
	EffectFilterDialog dialog(rows(), {QStringLiteral("Dissolve")}, {});
	auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("effectSearch"));
	auto *choices = dialog.findChild<QTreeWidget *>(QStringLiteral("effectChoices"));
	auto *volumes = dialog.findChild<QComboBox *>(QStringLiteral("effectVolume"));
	QVERIFY(search && choices && volumes);
	search->setText(QStringLiteral("3d warp"));
	QVERIFY(!choices->topLevelItem(0)->isHidden());
	QVERIFY(choices->topLevelItem(1)->isHidden());
	QCOMPARE(dialog.selectedEffects(), QStringList{QStringLiteral("Dissolve")});
	choices->topLevelItem(0)->setCheckState(0, Qt::Checked);
	QCOMPARE(dialog.selectedEffects().size(), 2);
	volumes->setCurrentIndex(volumes->findData(QStringLiteral("/Volumes/EDIT")));
	QCOMPARE(dialog.selectedEffects(), QStringList{QStringLiteral("3D Warp")});
	search->setText(QStringLiteral("blend"));
	QVERIFY(!choices->topLevelItem(0)->isHidden());
}

void TestEffectFilterDialog::clear_returns_no_hidden_filter()
{
	EffectFilterDialog dialog(rows(), {QStringLiteral("3D Warp")}, QStringLiteral("/Volumes/EDIT"));
	auto *clear = dialog.findChild<QPushButton *>(QStringLiteral("clearEffectFilter"));
	QVERIFY(clear);
	clear->click();
	QCOMPARE(dialog.result(), int(QDialog::Accepted));
	QVERIFY(dialog.selectedEffects().isEmpty());
	QVERIFY(dialog.selectedVolume().isEmpty());
}

QTEST_MAIN(TestEffectFilterDialog)
#include "tst_effectfilterdialog.moc"
