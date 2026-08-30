#include "oprequest.h"

#include <QTest>

// The on-disk names are a contract: a ledger written today must mean the
// same thing to every future build, so the name<->enum mapping is pinned
// here in both directions, and unknown names must come back empty rather
// than guessed (a guessed kind is how recovery turns a crash into loss).
class TestOpRequest : public QObject
{
	Q_OBJECT
private slots:
	void kind_names_round_trip();
	void kind_unknown_name_is_refused();
	void policy_names_round_trip();
	void policy_unknown_name_is_refused();
};

void TestOpRequest::kind_names_round_trip()
{
	const OpKind kinds[] = {OpKind::Copy, OpKind::Move, OpKind::Delete, OpKind::Rename,
							OpKind::Undo};
	for (const OpKind k : kinds)
	{
		const auto back = opKindFromName(opKindName(k));
		QVERIFY(back.has_value());
		QCOMPARE(*back, k);
	}
	// The exact spellings are load-bearing (they live in ledger files).
	QCOMPARE(opKindName(OpKind::Copy), QStringLiteral("copy"));
	QCOMPARE(opKindName(OpKind::Move), QStringLiteral("move"));
	QCOMPARE(opKindName(OpKind::Delete), QStringLiteral("delete"));
	QCOMPARE(opKindName(OpKind::Rename), QStringLiteral("rename"));
	QCOMPARE(opKindName(OpKind::Undo), QStringLiteral("undo"));
}

void TestOpRequest::kind_unknown_name_is_refused()
{
	QVERIFY(!opKindFromName(QStringLiteral("bogus")).has_value());
	QVERIFY(!opKindFromName(QString()).has_value());
	// Case matters: the writer always emits lowercase, so anything else
	// is not one of ours.
	QVERIFY(!opKindFromName(QStringLiteral("Copy")).has_value());
}

void TestOpRequest::policy_names_round_trip()
{
	const ConflictPolicy policies[] = {ConflictPolicy::KeepBoth, ConflictPolicy::Skip,
									   ConflictPolicy::Replace};
	for (const ConflictPolicy p : policies)
	{
		const auto back = conflictPolicyFromName(conflictPolicyName(p));
		QVERIFY(back.has_value());
		QCOMPARE(*back, p);
	}
	QCOMPARE(conflictPolicyName(ConflictPolicy::KeepBoth), QStringLiteral("keepboth"));
	QCOMPARE(conflictPolicyName(ConflictPolicy::Skip), QStringLiteral("skip"));
	QCOMPARE(conflictPolicyName(ConflictPolicy::Replace), QStringLiteral("replace"));
}

void TestOpRequest::policy_unknown_name_is_refused()
{
	QVERIFY(!conflictPolicyFromName(QStringLiteral("bogus")).has_value());
	QVERIFY(!conflictPolicyFromName(QString()).has_value());
}

QTEST_APPLESS_MAIN(TestOpRequest)
#include "tst_oprequest.moc"
