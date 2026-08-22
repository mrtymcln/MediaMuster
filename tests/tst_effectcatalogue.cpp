// EffectCatalogue: precompute clip name → Avid effect name + palette
// category. The 15 real render names in the corpus (14 match; the 15th is a
// user-typed title), the parse rule (last comma, +N), the mangle rule
// (space/colon → underscore), ambiguity, and a localised spelling.

#include "effectcatalogue.h"

#include <QTest>

class TestEffectCatalogue : public QObject
{
	Q_OBJECT
private slots:
	void catalogue_loads_from_the_resource();
	void corpus_render_names_resolve();
	void user_typed_title_is_not_a_standard_effect();
	void parse_splits_on_the_last_comma_and_strips_the_instance();
	void mangle_turns_spaces_and_colons_into_underscores();
	void ambiguous_names_report_every_category();
	void localised_spellings_resolve_to_the_english_name();
};

void TestEffectCatalogue::catalogue_loads_from_the_resource()
{
	QVERIFY2(EffectCatalogue::size() >= 880, qPrintable(QString::number(EffectCatalogue::size())));
}

void TestEffectCatalogue::corpus_render_names_resolve()
{
	struct Case
	{
		const char *clip;
		const char *name;
		const char *category;
		const char *sequence;
		int instance;
	};
	// The MaterialPackage names of the corpus's 15 precomputes (8 distinct
	// effect tokens), as MXF/MDB spell them.
	const Case cases[] = {
		{"zT_\xc3\x9ft_1080i_50_seq,Color_Correction+1", "Color Correction", "Image", "zT_\xc3\x9ft_1080i_50_seq", 1},
		{"zT_\xc3\x9ft_1080i_50_seq,AniMatte+6", "AniMatte", "Key", "zT_\xc3\x9ft_1080i_50_seq", 6},
		{"Untitled_Sequence.01,3D_Ball+4", "3D Ball", "Xpress 3D Effect", "Untitled_Sequence.01", 4},
		{"zT_\xc3\x9ft_1080i_50_seq,1.85_Mask+2", "1.85 Mask", "Film", "zT_\xc3\x9ft_1080i_50_seq", 2},
		{"zT_\xc3\x9ft_1080i_50_seq,3D_Page_Fold+5", "3D Page Fold", "Xpress 3D Effect", "zT_\xc3\x9ft_1080i_50_seq", 5},
		{"zT_\xc3\x9ft_1080i_50_seq,14_9_Letterbox+4", "14:9 Letterbox", "Reformat", "zT_\xc3\x9ft_1080i_50_seq", 4},
		{"Untitled_Sequence.01,Color_Adapter+9", "Color Adapter", "Image", "Untitled_Sequence.01", 9},
		{"Untitled_Sequence.04,Color_Correction+3", "Color Correction", "Image", "Untitled_Sequence.04", 3},
	};
	for (const Case &c : cases)
	{
		const EffectCatalogue::Hit h = EffectCatalogue::lookup(QString::fromUtf8(c.clip));
		QVERIFY2(h.matched, c.clip);
		QCOMPARE(h.name, QString::fromUtf8(c.name));
		QCOMPARE(h.category, QString::fromUtf8(c.category));
		QCOMPARE(h.sequence, QString::fromUtf8(c.sequence));
		QCOMPARE(h.instance, c.instance);
	}
}

void TestEffectCatalogue::user_typed_title_is_not_a_standard_effect()
{
	// The 15th corpus render: a title whose text the user typed.
	const EffectCatalogue::Hit h = EffectCatalogue::lookup(QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq,10101010+3"));
	QVERIFY(!h.matched);
	QCOMPARE(h.name, QStringLiteral("10101010"));
	QCOMPARE(h.category, QStringLiteral("Not a standard Avid effect"));
	QCOMPARE(h.sequence, QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq"));
	QCOMPARE(h.instance, 3);
}

void TestEffectCatalogue::parse_splits_on_the_last_comma_and_strips_the_instance()
{
	// A sequence name containing a comma: the separator is the LAST one.
	EffectCatalogue::Hit h = EffectCatalogue::lookup(QStringLiteral("Act_1,_Scene_2,Dissolve+12"));
	QVERIFY(h.matched);
	QCOMPARE(h.name, QStringLiteral("Dissolve"));
	QCOMPARE(h.sequence, QStringLiteral("Act_1,_Scene_2"));
	QCOMPARE(h.instance, 12);

	// No comma and no instance: the whole name is the token, nothing invented.
	h = EffectCatalogue::lookup(QStringLiteral("Dissolve"));
	QVERIFY(h.matched);
	QVERIFY(h.sequence.isEmpty());
	QCOMPARE(h.instance, 0);

	// A name that is not a render shape at all comes back verbatim, unmatched.
	h = EffectCatalogue::lookup(QStringLiteral("Interview Take 3"));
	QVERIFY(!h.matched);
	QCOMPARE(h.name, QStringLiteral("Interview Take 3"));

	// Empty in, empty-but-safe out.
	h = EffectCatalogue::lookup(QString());
	QVERIFY(!h.matched);
	QVERIFY(h.name.isEmpty());
}

void TestEffectCatalogue::mangle_turns_spaces_and_colons_into_underscores()
{
	QCOMPARE(EffectCatalogue::mangle(QStringLiteral("14:9 Letterbox")), QStringLiteral("14_9_Letterbox"));
	QCOMPARE(EffectCatalogue::mangle(QStringLiteral("1.85 Mask")), QStringLiteral("1.85_Mask")); // the dot survives
	QCOMPARE(EffectCatalogue::mangle(QStringLiteral("AniMatte")), QStringLiteral("AniMatte"));
}

void TestEffectCatalogue::ambiguous_names_report_every_category()
{
	const EffectCatalogue::Hit h = EffectCatalogue::lookup(QStringLiteral("seq,Bottom_to_Top+1"));
	QVERIFY(h.matched);
	QCOMPARE(h.name, QStringLiteral("Bottom to Top"));
	QVERIFY2(h.category.contains(QStringLiteral("Conceal")), qPrintable(h.category));
	QVERIFY2(h.category.contains(QStringLiteral("Push")), qPrintable(h.category));
	QVERIFY2(h.category.contains(QStringLiteral(" / ")), qPrintable(h.category));
}

void TestEffectCatalogue::localised_spellings_resolve_to_the_english_name()
{
	// A German Media Composer writes "Blende" for Dissolve; the catalogue
	// keys every shipped language so the count stays one effect, not five.
	const EffectCatalogue::Hit de = EffectCatalogue::lookup(QStringLiteral("Sequenz_1,Blende+2"));
	QVERIFY(de.matched);
	QCOMPARE(de.name, QStringLiteral("Dissolve"));
	QCOMPARE(de.category, QStringLiteral("Blend"));
	const EffectCatalogue::Hit fr = EffectCatalogue::lookup(QStringLiteral("Seq,Correction_colorimétrique+1"));
	QVERIFY(fr.matched);
	QCOMPARE(fr.name, QStringLiteral("Color Correction"));
}

QTEST_APPLESS_MAIN(TestEffectCatalogue)
#include "tst_effectcatalogue.moc"
