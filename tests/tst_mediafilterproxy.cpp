// MediaFilterProxy search behaviour across Unicode normalisation forms.
// macOS volumes often hand back decomposed (NFD) filenames — 'é' stored as
// 'e' + combining acute — which render identically to composed (NFC)
// keyboard input but are different code points. Search must treat the two
// forms as the same text while keeping accents themselves significant.
// Raw escape sequences throughout so source-file encoding can't drift what
// the assertions actually test.

#include "enumutil.h"
#include "mediafile.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"

#include <QTest>

namespace
{
	MediaFile rowNamed(const QString &clipName)
	{
		MediaFile f;
		f.clipName = clipName;
		f.fileName = clipName + QStringLiteral(".mxf");
		f.filePath = QStringLiteral("/vol/") + f.fileName;
		return f;
	}

	// "café" composed: one precomposed é (U+00E9).
	const QString kCafeNfc = QStringLiteral("café");
	// "café" decomposed: 'e' + combining acute (U+0301) — what APFS/SMB
	// paths frequently contain.
	const QString kCafeNfd = QStringLiteral("café");
} // namespace

class TestMediaFilterProxy : public QObject
{
	Q_OBJECT
private slots:
	void nfc_search_finds_nfd_row();
	void nfd_search_finds_nfc_row();
	void folding_and_normalisation_compose();
	void plain_ascii_never_matches_accents();

	// Sorting. The Size column sorts on exact byte counts, not the
	// rounded MB display string — two files that both show "850.0 MB"
	// still order by their real sizes, and no double rounding sits
	// between the user and the answer.
	void size_column_sorts_on_exact_bytes();
};

void TestMediaFilterProxy::nfc_search_finds_nfd_row()
{
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfd)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	QVERIFY(kCafeNfc != kCafeNfd); // the two forms really are different code points
	proxy.setSearchText(kCafeNfc); // composed keyboard input
	QCOMPARE(proxy.rowCount(), 1); // used to be 0 — the invisible-file bug
}

void TestMediaFilterProxy::nfd_search_finds_nfc_row()
{
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfc)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(kCafeNfd); // e.g. pasted from an NFD path
	QCOMPARE(proxy.rowCount(), 1);
}

void TestMediaFilterProxy::folding_and_normalisation_compose()
{
	// Uppercase decomposed row, lowercase composed needle: both the case
	// fold and the normalisation have to apply for this to hit.
	MediaTableModel model;
	model.setMediaFiles({rowNamed(QStringLiteral("CAFÉ REEL 7"))});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(kCafeNfc);
	QCOMPARE(proxy.rowCount(), 1);
}

void TestMediaFilterProxy::plain_ascii_never_matches_accents()
{
	// Accents stay significant; only the FORM is insensitive. This must
	// hold for both storage forms — before the fix an NFD row matched the
	// bare-ASCII needle ("cafe" is literally a prefix of "cafe" + accent)
	// while an NFC row didn't, so results depended on which volume a file
	// came from.
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfc), rowNamed(kCafeNfd)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(QStringLiteral("cafe"));
	QCOMPARE(proxy.rowCount(), 0);

	// Sanity: the accented needle still finds both rows.
	proxy.setSearchText(kCafeNfc);
	QCOMPARE(proxy.rowCount(), 2);
}

void TestMediaFilterProxy::size_column_sorts_on_exact_bytes()
{
	const auto sized = [](const QString &name, qint64 bytes)
	{
		MediaFile f = rowNamed(name);
		f.sizeBytes = bytes;
		return f;
	};

	// The two 850.0-rounding neighbours differ by 400 bytes: the display
	// string cannot tell them apart, the sort must.
	MediaTableModel model;
	model.setMediaFiles({sized(QStringLiteral("big"), 1'100'000'000),
						 sized(QStringLiteral("mid_hi"), 850'000'400),
						 sized(QStringLiteral("mid_lo"), 850'000'000),
						 sized(QStringLiteral("empty"), 0)});

	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int sizeCol = Enum::to_underlying(MediaTableModel::Column::SizeMB);
	const int nameCol = Enum::to_underlying(MediaTableModel::Column::ClipName);
	proxy.sort(sizeCol, Qt::AscendingOrder);
	QCOMPARE(proxy.rowCount(), 4);

	QStringList order;
	for (int r = 0; r < proxy.rowCount(); ++r)
		order << proxy.data(proxy.index(r, nameCol)).toString();
	QCOMPARE(order, (QStringList{QStringLiteral("empty"), QStringLiteral("mid_lo"),
								 QStringLiteral("mid_hi"), QStringLiteral("big")}));

	// The two neighbours really do render identically — proof the order
	// above cannot have come from the display string.
	QCOMPARE(model.fileAt(1).sizeMBDisplay(), model.fileAt(2).sizeMBDisplay());
}

QTEST_GUILESS_MAIN(TestMediaFilterProxy)
#include "tst_mediafilterproxy.moc"
