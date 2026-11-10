#include "scanner/mob_id.h"

#include <QByteArray>
#include <QHash>
#include <QtTest>
#include <array>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

static_assert(!std::is_default_constructible_v<Bella::MobId>);

// MARK: - Test helpers

namespace
{
	using Layout = Bella::MobIdLayout;
	using Order = Bella::ByteOrder;
	using Kind = Bella::MobIdKind;

	struct ConversionExample
	{
		const char *name;
		Layout layout;
		Order order;
		Kind kind;
		QByteArray stored;
		QByteArray expected;
	};
}

// MARK: - Test fixture

class TestBellaMobId final : public QObject
{
	Q_OBJECT
private slots:
	void complete_layouts_have_explicit_canonical_bytes();
	void wrapped_modern_id_uses_modern_numeric_fields();
	void declared_order_is_not_guessed();
	void wrong_widths_return_invalid_width();
	void zero_values_and_unfamiliar_labels_are_preserved();
	void equivalent_layouts_share_one_hash_key();
	void every_modern_byte_participates_in_equality();
	void unfamiliar_legacy_organization_is_part_of_the_key();
	void legacy_and_modern_forms_remain_distinct();
	void results_own_their_bytes_through_copy_and_move();
};

// MARK: - Canonical layouts and byte order

void TestBellaMobId::complete_layouts_have_explicit_canonical_bytes()
{
	// These are authored values, including the big-endian examples. Expected
	// bytes spell the numeric fields in little-endian order on every host.
	const QByteArray modern = QByteArray::fromHex(
		"000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f");
	const QByteArray wrapped = QByteArray::fromHex(
		"060a2b340101010101010f001300000044332211d4c3b2a1060e2b347f7f2a80");
	const QByteArray unfamiliar = QByteArray::fromHex("0700000044332211d4c3b2a1");
	const std::array<ConversionExample, 8> examples{{
		{"modern little-endian", Layout::Aaf32, Order::LittleEndian, Kind::Aaf32,
		 modern, modern},
		{"authored modern big-endian", Layout::Aaf32, Order::BigEndian, Kind::Aaf32,
		 QByteArray::fromHex("000102030405060708090a0b0c0d0e0f112233445566778818191a1b1c1d1e1f"), modern},
		{"legacy PMR little-endian", Layout::PmrLegacy8, Order::LittleEndian, Kind::Aaf32,
		 QByteArray::fromHex("44332211d4c3b2a1"), wrapped},
		{"authored legacy PMR big-endian", Layout::PmrLegacy8, Order::BigEndian, Kind::Aaf32,
		 QByteArray::fromHex("11223344a1b2c3d4"), wrapped},
		{"organization-42 little-endian", Layout::OmfUid12, Order::LittleEndian, Kind::Aaf32,
		 QByteArray::fromHex("2a00000044332211d4c3b2a1"), wrapped},
		{"authored organization-42 big-endian", Layout::OmfUid12, Order::BigEndian, Kind::Aaf32,
		 QByteArray::fromHex("0000002a11223344a1b2c3d4"), wrapped},
		{"unfamiliar organization little-endian", Layout::OmfUid12, Order::LittleEndian, Kind::OmfUid12,
		 unfamiliar, unfamiliar},
		{"authored unfamiliar organization big-endian", Layout::OmfUid12, Order::BigEndian, Kind::OmfUid12,
		 QByteArray::fromHex("0000000711223344a1b2c3d4"), unfamiliar},
	}};
	for (const auto &example : examples)
	{
		const auto result = Bella::normalizeMobId(example.stored, example.layout, example.order);
		const auto *id = std::get_if<Bella::MobId>(&result);
		QVERIFY2(id, example.name);
		QVERIFY2(id->kind() == example.kind, example.name);
		QCOMPARE(id->bytes().toByteArray(), example.expected);
	}
}

void TestBellaMobId::wrapped_modern_id_uses_modern_numeric_fields()
{
	// An Avid wrapper does not change the stored modern 32/16/16-bit layout
	// into the legacy two-32-bit layout. This input is authored big-endian.
	const QByteArray stored = QByteArray::fromHex(
		"060a2b340101010101010f001300000011223344c3d4a1b2060e2b347f7f2a80");
	const QByteArray expected = QByteArray::fromHex(
		"060a2b340101010101010f001300000044332211d4c3b2a1060e2b347f7f2a80");
	const auto result = Bella::normalizeMobId(stored, Layout::Aaf32, Order::BigEndian);
	const auto *id = std::get_if<Bella::MobId>(&result);
	QVERIFY(id);
	QVERIFY(id->kind() == Kind::Aaf32);
	QCOMPARE(id->bytes().toByteArray(), expected);
}

void TestBellaMobId::declared_order_is_not_guessed()
{
	// With this declared order, the organization is 0x2a000000, not 42.
	// Do not try the opposite order merely to find the recognized mapping.
	const QByteArray stored = QByteArray::fromHex("2a00000044332211d4c3b2a1");
	const auto result = Bella::normalizeMobId(stored, Layout::OmfUid12, Order::BigEndian);
	const auto *id = std::get_if<Bella::MobId>(&result);
	QVERIFY(id);
	QVERIFY(id->kind() == Kind::OmfUid12);
	QCOMPARE(id->bytes().toByteArray(), QByteArray::fromHex("0000002a11223344a1b2c3d4"));
}

void TestBellaMobId::wrong_widths_return_invalid_width()
{
	const std::array<std::pair<Layout, qsizetype>, 3> layouts{{
		{Layout::PmrLegacy8, 8},
		{Layout::OmfUid12, 12},
		{Layout::Aaf32, 32},
	}};
	const std::array<qsizetype, 13> widths{{0, 1, 7, 8, 9, 11, 12, 13, 31, 32, 33, 64, 4096}};
	for (const auto &[layout, expectedWidth] : layouts)
		for (const auto order : {Order::LittleEndian, Order::BigEndian})
			for (const auto width : widths)
			{
				if (width == expectedWidth)
					continue;
				const QByteArray bytes(width, '\0');
				const auto result = Bella::normalizeMobId(bytes, layout, order);
				const auto *error = std::get_if<Bella::MobIdError>(&result);
				QVERIFY(error);
				QVERIFY(*error == Bella::MobIdError::InvalidWidth);
			}

	const auto empty = Bella::normalizeMobId(QByteArrayView{}, Layout::Aaf32, Order::LittleEndian);
	QVERIFY(std::holds_alternative<Bella::MobIdError>(empty));
}

void TestBellaMobId::zero_values_and_unfamiliar_labels_are_preserved()
{
	const QByteArray zeroModern(32, '\0');
	const QByteArray zeroLegacy(12, '\0');
	const QByteArray wrappedZero = QByteArray::fromHex(
		"060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80");
	const QByteArray arbitraryLabel = QByteArray::fromHex(
		"ffffffffffffffffffffffffffffffff4433221166558877ffffffffffffffff");
	const std::array<ConversionExample, 4> examples{{
		{"zero modern ID", Layout::Aaf32, Order::LittleEndian, Kind::Aaf32, zeroModern, zeroModern},
		{"zero legacy PMR ID", Layout::PmrLegacy8, Order::LittleEndian, Kind::Aaf32,
		 QByteArray(8, '\0'), wrappedZero},
		{"zero legacy UID", Layout::OmfUid12, Order::LittleEndian, Kind::OmfUid12, zeroLegacy, zeroLegacy},
		{"unfamiliar modern label", Layout::Aaf32, Order::LittleEndian, Kind::Aaf32,
		 arbitraryLabel, arbitraryLabel},
	}};
	for (const auto &example : examples)
	{
		const auto result = Bella::normalizeMobId(example.stored, example.layout, example.order);
		const auto *id = std::get_if<Bella::MobId>(&result);
		QVERIFY2(id, example.name);
		QVERIFY2(id->kind() == example.kind, example.name);
		QCOMPARE(id->bytes().toByteArray(), example.expected);
	}
	// Whether a particular field uses a null sentinel belongs to that reader,
	// rather than a blanket zero/prefix rejection in this conversion helper.
}

// MARK: - Identity equality and owned storage

void TestBellaMobId::equivalent_layouts_share_one_hash_key()
{
	const auto pmr = Bella::normalizeMobId(QByteArray::fromHex("44332211d4c3b2a1"),
										   Layout::PmrLegacy8, Order::LittleEndian);
	const auto omf = Bella::normalizeMobId(QByteArray::fromHex("0000002a11223344a1b2c3d4"),
										   Layout::OmfUid12, Order::BigEndian);
	const auto modern = Bella::normalizeMobId(QByteArray::fromHex(
												  "060a2b340101010101010f001300000011223344c3d4a1b2060e2b347f7f2a80"),
											  Layout::Aaf32, Order::BigEndian);
	const auto *pmrId = std::get_if<Bella::MobId>(&pmr);
	const auto *omfId = std::get_if<Bella::MobId>(&omf);
	const auto *modernId = std::get_if<Bella::MobId>(&modern);
	QVERIFY(pmrId);
	QVERIFY(omfId);
	QVERIFY(modernId);
	QVERIFY(*pmrId == *omfId);
	QVERIFY(*omfId == *modernId);
	QVERIFY(!(*pmrId != *modernId));
	for (const size_t seed : {size_t{0}, size_t{0x1234}, std::numeric_limits<size_t>::max()})
	{
		QCOMPARE(Bella::qHash(*pmrId, seed), Bella::qHash(*omfId, seed));
		QCOMPARE(Bella::qHash(*omfId, seed), Bella::qHash(*modernId, seed));
	}
	QHash<Bella::MobId, int> index;
	index.insert(*pmrId, 1);
	index.insert(*omfId, 2);
	index.insert(*modernId, 3);
	QCOMPARE(index.size(), qsizetype{1});
	QCOMPARE(index.value(*pmrId), 3);
}

void TestBellaMobId::every_modern_byte_participates_in_equality()
{
	const QByteArray original = QByteArray::fromHex(
		"000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f");
	const auto result = Bella::normalizeMobId(original, Layout::Aaf32, Order::LittleEndian);
	const auto *id = std::get_if<Bella::MobId>(&result);
	QVERIFY(id);
	QHash<Bella::MobId, int> index;
	index.insert(*id, -1);
	for (qsizetype offset = 0; offset < original.size(); ++offset)
	{
		QByteArray changed = original;
		changed[offset] = static_cast<char>(static_cast<unsigned char>(changed[offset]) ^ 1U);
		const auto changedResult = Bella::normalizeMobId(changed, Layout::Aaf32, Order::LittleEndian);
		const auto *changedId = std::get_if<Bella::MobId>(&changedResult);
		QVERIFY(changedId);
		QVERIFY(*id != *changedId);
		QVERIFY(!(*id == *changedId));
		index.insert(*changedId, static_cast<int>(offset));
	}
	// Different keys may legitimately hash alike; full equality must still
	// preserve every entry, including differences outside bytes 16..23.
	QCOMPARE(index.size(), original.size() + 1);
	QCOMPARE(index.value(*id), -1);
	for (qsizetype offset = 0; offset < original.size(); ++offset)
	{
		QByteArray changed = original;
		changed[offset] = static_cast<char>(static_cast<unsigned char>(changed[offset]) ^ 1U);
		const auto changedResult = Bella::normalizeMobId(changed, Layout::Aaf32, Order::LittleEndian);
		const auto *changedId = std::get_if<Bella::MobId>(&changedResult);
		QVERIFY(changedId);
		QCOMPARE(index.value(*changedId, -2), static_cast<int>(offset));
	}
}

void TestBellaMobId::unfamiliar_legacy_organization_is_part_of_the_key()
{
	const auto first = Bella::normalizeMobId(QByteArray::fromHex("0700000044332211d4c3b2a1"),
											 Layout::OmfUid12, Order::LittleEndian);
	const auto second = Bella::normalizeMobId(QByteArray::fromHex("0800000044332211d4c3b2a1"),
											  Layout::OmfUid12, Order::LittleEndian);
	const auto sameFirst = Bella::normalizeMobId(QByteArray::fromHex("0000000711223344a1b2c3d4"),
												 Layout::OmfUid12, Order::BigEndian);
	const auto *firstId = std::get_if<Bella::MobId>(&first);
	const auto *secondId = std::get_if<Bella::MobId>(&second);
	const auto *sameFirstId = std::get_if<Bella::MobId>(&sameFirst);
	QVERIFY(firstId);
	QVERIFY(secondId);
	QVERIFY(sameFirstId);
	QVERIFY(firstId->kind() == Kind::OmfUid12);
	QVERIFY(secondId->kind() == Kind::OmfUid12);
	QCOMPARE(firstId->bytes().size(), qsizetype{12});
	QVERIFY(*firstId != *secondId);
	QVERIFY(*firstId == *sameFirstId);
	QCOMPARE(Bella::qHash(*firstId, size_t{17}), Bella::qHash(*sameFirstId, size_t{17}));
	QHash<Bella::MobId, int> index;
	index.insert(*firstId, 7);
	index.insert(*secondId, 8);
	QCOMPARE(index.size(), qsizetype{2});
	QCOMPARE(index.value(*sameFirstId), 7);
	QCOMPARE(index.value(*secondId), 8);
}

void TestBellaMobId::legacy_and_modern_forms_remain_distinct()
{
	const QByteArray legacy = QByteArray::fromHex("0700000044332211d4c3b2a1");
	const QByteArray modern = legacy + QByteArray(20, '\0');
	const auto legacyResult = Bella::normalizeMobId(legacy, Layout::OmfUid12, Order::LittleEndian);
	const auto modernResult = Bella::normalizeMobId(modern, Layout::Aaf32, Order::LittleEndian);
	const auto *legacyId = std::get_if<Bella::MobId>(&legacyResult);
	const auto *modernId = std::get_if<Bella::MobId>(&modernResult);
	QVERIFY(legacyId);
	QVERIFY(modernId);
	QVERIFY(legacyId->kind() == Kind::OmfUid12);
	QVERIFY(modernId->kind() == Kind::Aaf32);
	QCOMPARE(legacyId->bytes().size(), qsizetype{12});
	QCOMPARE(modernId->bytes().size(), qsizetype{32});
	QVERIFY(*legacyId != *modernId);
	QHash<Bella::MobId, int> index;
	index.insert(*legacyId, 12);
	index.insert(*modernId, 32);
	QCOMPARE(index.size(), qsizetype{2});
	QCOMPARE(index.value(*legacyId), 12);
	QCOMPARE(index.value(*modernId), 32);
}

void TestBellaMobId::results_own_their_bytes_through_copy_and_move()
{
	const std::array<ConversionExample, 3> examples{{
		{"modern ownership", Layout::Aaf32, Order::LittleEndian, Kind::Aaf32,
		 QByteArray::fromHex("000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f"),
		 QByteArray::fromHex("000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f")},
		{"legacy UID ownership", Layout::OmfUid12, Order::LittleEndian, Kind::OmfUid12,
		 QByteArray::fromHex("0700000044332211d4c3b2a1"), QByteArray::fromHex("0700000044332211d4c3b2a1")},
		{"legacy PMR ownership", Layout::PmrLegacy8, Order::LittleEndian, Kind::Aaf32,
		 QByteArray::fromHex("44332211d4c3b2a1"),
		 QByteArray::fromHex("060a2b340101010101010f001300000044332211d4c3b2a1060e2b347f7f2a80")},
	}};
	for (const auto &example : examples)
	{
		auto result = [&example]
		{
			// Force a separate allocation: implicit sharing with the example
			// would keep the original bytes alive when source.fill() detaches.
			QByteArray source(example.stored.constData(), example.stored.size());
			auto parsed = Bella::normalizeMobId(source, example.layout, example.order);
			source.fill(static_cast<char>(0xcc));
			return parsed;
		}(); // The source is already destroyed when returned bytes are inspected.
		const auto copied = result;
		const auto moved = std::move(result);
		const auto *copiedId = std::get_if<Bella::MobId>(&copied);
		const auto *movedId = std::get_if<Bella::MobId>(&moved);
		QVERIFY2(copiedId, example.name);
		QVERIFY2(movedId, example.name);
		QCOMPARE(copiedId->bytes().toByteArray(), example.expected);
		QCOMPARE(movedId->bytes().toByteArray(), example.expected);
		QVERIFY(*copiedId == *movedId);
	}
}

QTEST_APPLESS_MAIN(TestBellaMobId)
#include "tst_bella_mob_id.moc"
