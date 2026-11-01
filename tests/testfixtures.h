#pragma once

#include <QDir>
#include <QTest>

namespace TestFixtures
{
	inline bool available()
	{
		return QDir(QStringLiteral(FIXTURES_DIR)).exists();
	}
}

// Call at the test entry point so QSKIP returns from the test, not a helper.
#define REQUIRE_TEST_FIXTURES() \
	do \
	{ \
		if (!TestFixtures::available()) \
			QSKIP("Shared real-media fixtures are not available."); \
	} while (false)
