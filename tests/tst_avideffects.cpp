// AvidEffects: precompute clip name → Avid effect name + palette
// category. The 15 real render names in the corpus (14 match; the 15th is a
// user-typed title), current 26.8 registrations, the parse rule, the mangle rule
// (space/colon → underscore), ambiguity, and a localised spelling.

#include "avideffects.h"

#include <QTest>

class TestAvidEffects : public QObject
{
	Q_OBJECT
private slots:
	void the_whole_catalogue_is_compiled_in();
	void corpus_render_names_resolve();
	void current_registration_variants_and_render_names();
	void aliases_do_not_guess_arbitrary_names();
	void user_typed_title_is_not_a_standard_effect();
	void parse_splits_on_the_last_comma_and_strips_the_instance();
	void comma_instances_data();
	void comma_instances();
	void invalid_suffixes_stay_visible_data();
	void invalid_suffixes_stay_visible();
	void mangle_turns_spaces_and_colons_into_underscores();
	void ambiguous_names_report_every_category();
	void localised_spellings_resolve_to_the_english_name();
};

void TestAvidEffects::the_whole_catalogue_is_compiled_in()
{
	// The fresh 26.8 extraction produces 887 distinct name/category pairs.
	// This checks the generated include is complete; it does not count availability.
	QCOMPARE(AvidEffects::size(), 887);
}

void TestAvidEffects::corpus_render_names_resolve()
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
		const AvidEffects::Hit h = AvidEffects::lookup(QString::fromUtf8(c.clip));
		QVERIFY2(h.matched, c.clip);
		QCOMPARE(h.name, QString::fromUtf8(c.name));
		QCOMPARE(h.category, QString::fromUtf8(c.category));
		QCOMPARE(h.sequence, QString::fromUtf8(c.sequence));
		QCOMPARE(h.instance, c.instance);
	}
}

void TestAvidEffects::current_registration_variants_and_render_names()
{
	struct Case
	{
		const char *token;
		const char *name;
		const char *category;
	};
	const Case cases[] = {
		// The observed compatibility spelling and both current AlphaFlex states.
		{"3DWarp", "3D Warp", "Blend"},
		{"3D_Warp", "3D Warp", "Blend"},
		{"3D_Warp_Legacy", "3D Warp Legacy", "Legacy"},
		{"Transformation", "Transformation", "Alpha"},
		{"Audio_Dissolve", "Audio Dissolve", "Blend"},
		{"Motion_Effect", "Motion Effect", "Timewarp"},
		{"D-Verb", "D-Verb", "AudioSuite"},
		// Current MCEffects registrations; the Legacy name is conditional.
		{"SpectraMatte_Legacy", "SpectraMatte Legacy", "Legacy"},
		{"SpectraMatte_A", "SpectraMatte A", "Alpha"},
		{"Particle_Orbit", "Particle Orbit", "Illusion FX"},
	};
	for (const Case &c : cases)
	{
		const auto hit = AvidEffects::lookup(QStringLiteral("Act_1,_Scene_2,") +
											 QString::fromUtf8(c.token) + QStringLiteral(",79.new.01"));
		QVERIFY2(hit.matched, c.token);
		QCOMPARE(hit.name, QString::fromUtf8(c.name));
		QCOMPARE(hit.category, QString::fromUtf8(c.category));
		QCOMPARE(hit.sequence, QStringLiteral("Act_1,_Scene_2"));
		QCOMPARE(hit.instance, 79);
	}
}

void TestAvidEffects::aliases_do_not_guess_arbitrary_names()
{
	for (const QString &token : {QStringLiteral("TITLE"), QStringLiteral("SLATE"),
								 QStringLiteral("3dwarp"), QStringLiteral("AudioDissolve"),
								 QStringLiteral("MotionEffect"), QStringLiteral("DVerb"),
								 QStringLiteral("3DWarp_custom"), QStringLiteral("FXBaseProxyRegistration")})
	{
		const auto hit = AvidEffects::lookup(QStringLiteral("Seq,") + token + QStringLiteral("+2"));
		QVERIFY2(!hit.matched, qPrintable(token));
		QCOMPARE(hit.name, token);
		QCOMPARE(hit.category, QStringLiteral("unknown"));
	}
}

void TestAvidEffects::user_typed_title_is_not_a_standard_effect()
{
	// The 15th corpus render: a title whose text the user typed.
	const AvidEffects::Hit h = AvidEffects::lookup(QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq,10101010+3"));
	QVERIFY(!h.matched);
	QCOMPARE(h.name, QStringLiteral("10101010"));
	QCOMPARE(h.category, QStringLiteral("unknown"));
	QCOMPARE(h.sequence, QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq"));
	QCOMPARE(h.instance, 3);
}

void TestAvidEffects::parse_splits_on_the_last_comma_and_strips_the_instance()
{
	// A sequence name containing a comma: the separator is the LAST one.
	AvidEffects::Hit h = AvidEffects::lookup(QStringLiteral("Act_1,_Scene_2,Dissolve+12"));
	QVERIFY(h.matched);
	QCOMPARE(h.name, QStringLiteral("Dissolve"));
	QCOMPARE(h.sequence, QStringLiteral("Act_1,_Scene_2"));
	QCOMPARE(h.instance, 12);

	// No comma and no instance: the whole name is the token, nothing invented.
	h = AvidEffects::lookup(QStringLiteral("Dissolve"));
	QVERIFY(h.matched);
	QVERIFY(h.sequence.isEmpty());
	QCOMPARE(h.instance, 0);

	// A name that is not a render shape at all comes back verbatim, unmatched.
	h = AvidEffects::lookup(QStringLiteral("Interview Take 3"));
	QVERIFY(!h.matched);
	QCOMPARE(h.name, QStringLiteral("Interview Take 3"));

	// Empty in, empty-but-safe out.
	h = AvidEffects::lookup(QString());
	QVERIFY(!h.matched);
	QVERIFY(h.name.isEmpty());
}

void TestAvidEffects::comma_instances_data()
{
	QTest::addColumn<QString>("clip");
	QTest::addColumn<QString>("name");
	QTest::addColumn<QString>("category");
	QTest::addColumn<QString>("sequence");
	QTest::addColumn<int>("instance");
	QTest::addColumn<bool>("matched");

	// Actual 2026-09-05 export rows 235, 236 and 245. Their Effect column
	// used to show 4.new.01/9.new.01/79.new.01, and Sequence included the
	// real effect token. Current AAudioDissolve registers Audio Dissolve / Blend.
	QTest::newRow("real-title")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER,Title,4.new.01")
		<< QStringLiteral("Title") << QStringLiteral("Title")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER") << 4 << true;
	QTest::newRow("real-resize")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER,Resize,9.new.01")
		<< QStringLiteral("Resize") << QStringLiteral("Image")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER") << 9 << true;
	QTest::newRow("real-audio-dissolve")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER,Audio_Dissolve,79.new.01")
		<< QStringLiteral("Audio Dissolve") << QStringLiteral("Blend")
		<< QStringLiteral("HAA_1730_8647_SUBMASTER") << 79 << true;
	QTest::newRow("sequence-commas")
		<< QStringLiteral("Act_1,_Scene_2,Title,12")
		<< QStringLiteral("Title") << QStringLiteral("Title")
		<< QStringLiteral("Act_1,_Scene_2") << 12 << true;
	QTest::newRow("repeated-rename-suffixes")
		<< QStringLiteral("Act_1,_Scene_2,Resize,12.new.01.new.002")
		<< QStringLiteral("Resize") << QStringLiteral("Image")
		<< QStringLiteral("Act_1,_Scene_2") << 12 << true;
	QTest::newRow("zero-instance")
		<< QStringLiteral("Seq,Title,0")
		<< QStringLiteral("Title") << QStringLiteral("Title")
		<< QStringLiteral("Seq") << 0 << true;
	QTest::newRow("largest-instance")
		<< QStringLiteral("Seq,Title,2147483647.new.2147483647")
		<< QStringLiteral("Title") << QStringLiteral("Title")
		<< QStringLiteral("Seq") << 2147483647 << true;
	QTest::newRow("plus-instance-with-sequence-commas")
		<< QStringLiteral("Act_1,_Scene_2,Resize+9")
		<< QStringLiteral("Resize") << QStringLiteral("Image")
		<< QStringLiteral("Act_1,_Scene_2") << 9 << true;
}

void TestAvidEffects::comma_instances()
{
	QFETCH(QString, clip);
	QFETCH(QString, name);
	QFETCH(QString, category);
	QFETCH(QString, sequence);
	QFETCH(int, instance);
	QFETCH(bool, matched);
	const AvidEffects::Hit hit = AvidEffects::lookup(clip);
	QCOMPARE(hit.name, name);
	QCOMPARE(hit.category, category);
	QCOMPARE(hit.sequence, sequence);
	QCOMPARE(hit.instance, instance);
	QCOMPARE(hit.matched, matched);
}

void TestAvidEffects::invalid_suffixes_stay_visible_data()
{
	QTest::addColumn<QString>("clip");
	QTest::addColumn<QString>("name");
	QTest::addColumn<QString>("sequence");
	for (const QString &suffix : {
			 QStringLiteral("4.new."), QStringLiteral("4.new.one"),
			 QStringLiteral("4.new.01.extra"), QStringLiteral("4.new.01.02"),
			 QStringLiteral("4.NEW.01"), QStringLiteral("-4"), QStringLiteral("+4"),
			 QStringLiteral("4.0"), QStringLiteral("4 "), QStringLiteral("4\n"),
			 QStringLiteral("2147483648"), QStringLiteral("4.new.2147483648"),
			 QStringLiteral("999999999999999999999999999999999999")})
	{
		QTest::newRow(qPrintable(QStringLiteral("comma-") + suffix))
			<< QStringLiteral("Act_1,_Scene_2,Title,") + suffix
			<< suffix << QStringLiteral("Act_1,_Scene_2,Title");
	}
	QTest::newRow("missing-effect")
		<< QStringLiteral("Seq,,4") << QStringLiteral("4") << QStringLiteral("Seq,");
	QTest::newRow("only-one-comma")
		<< QStringLiteral("Seq,4.new.01") << QStringLiteral("4.new.01") << QStringLiteral("Seq");
	QTest::newRow("plus-overflow")
		<< QStringLiteral("Act_1,_Scene_2,Dissolve+2147483648")
		<< QStringLiteral("Dissolve+2147483648") << QStringLiteral("Act_1,_Scene_2");
	QTest::newRow("plus-malformed")
		<< QStringLiteral("Act_1,_Scene_2,Dissolve+one")
		<< QStringLiteral("Dissolve+one") << QStringLiteral("Act_1,_Scene_2");
}

void TestAvidEffects::invalid_suffixes_stay_visible()
{
	QFETCH(QString, clip);
	QFETCH(QString, name);
	QFETCH(QString, sequence);
	const AvidEffects::Hit hit = AvidEffects::lookup(clip);
	QCOMPARE(hit.name, name);
	QCOMPARE(hit.sequence, sequence);
	QCOMPARE(hit.instance, 0);
	QVERIFY(!hit.matched);
	QCOMPARE(hit.category, QStringLiteral("unknown"));
}

void TestAvidEffects::mangle_turns_spaces_and_colons_into_underscores()
{
	QCOMPARE(AvidEffects::mangle(QStringLiteral("14:9 Letterbox")), QStringLiteral("14_9_Letterbox"));
	QCOMPARE(AvidEffects::mangle(QStringLiteral("1.85 Mask")), QStringLiteral("1.85_Mask")); // the dot survives
	QCOMPARE(AvidEffects::mangle(QStringLiteral("AniMatte")), QStringLiteral("AniMatte"));
}

void TestAvidEffects::ambiguous_names_report_every_category()
{
	const AvidEffects::Hit h = AvidEffects::lookup(QStringLiteral("seq,Bottom_to_Top+1"));
	QVERIFY(h.matched);
	QCOMPARE(h.name, QStringLiteral("Bottom to Top"));
	QVERIFY2(h.category.contains(QStringLiteral("Conceal")), qPrintable(h.category));
	QVERIFY2(h.category.contains(QStringLiteral("Push")), qPrintable(h.category));
	QVERIFY2(h.category.contains(QStringLiteral(" / ")), qPrintable(h.category));
}

void TestAvidEffects::localised_spellings_resolve_to_the_english_name()
{
	// A German Media Composer writes "Blende" for Dissolve; the catalogue
	// keys every shipped language so the count stays one effect, not five.
	const AvidEffects::Hit de = AvidEffects::lookup(QStringLiteral("Sequenz_1,Blende+2"));
	QVERIFY(de.matched);
	QCOMPARE(de.name, QStringLiteral("Dissolve"));
	QCOMPARE(de.category, QStringLiteral("Blend"));
	const AvidEffects::Hit fr = AvidEffects::lookup(QStringLiteral("Seq,Correction_colorimétrique+1"));
	QVERIFY(fr.matched);
	QCOMPARE(fr.name, QStringLiteral("Color Correction"));
}

QTEST_APPLESS_MAIN(TestAvidEffects)
#include "tst_avideffects.moc"
