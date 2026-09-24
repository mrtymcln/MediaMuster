#pragma once

// Hides list & table rows from macOS Accessibility to work around QTBUG-119526.
namespace QtAccessibilityFix
{
	void install();
} // namespace QtAccessibilityFix
