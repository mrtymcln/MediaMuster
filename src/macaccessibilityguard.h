#pragma once

#include <QtGlobal>

// MARK: - MacAccessibilityGuard

/// Works around a Qt 6.5 crash in the macOS accessibility bridge.
///
/// THE BUG (QTBUG-119526, P1). Qt's Cocoa plugin keeps its own array of a
/// list/table/tree's rows for VoiceOver and every other accessibility
/// client — and the OS queries that bridge on its own schedule, VoiceOver
/// or not. When a row becomes current while that array is stale, the
/// bridge indexes past its end, Cocoa throws NSRangeException, and because
/// the throw crosses Qt's C++ frames the process aborts. Seen in the field
/// on 2026-09-02: a folder dropped onto the volume list, one click on the
/// new row, SIGABRT in QListView::currentChanged → libqcocoa →
/// -[__NSArrayM objectAtIndexedSubscript:]. The same stack on Qt 6.5.1 is
/// in Wireshark's tracker (QTreeView). Qt hardened the bridge on
/// 2023-12-16 ("macOS a11y: rebuild table model if out-of-bounds cell is
/// requested", c81e3146, picked to 6.6 and 6.7) — two months after 6.5.3,
/// and 6.5.4 is a commercial-only release.
///
/// THE GUARD. Item views expose a "table" accessibility interface; that is
/// what makes the bridge build per-row elements at all. On macOS, with a Qt
/// older than the fix, hand every QListView / QTableView / QTreeView a
/// plain widget interface instead. No row elements means no stale row
/// array and no out-of-bounds lookup — the crash is impossible by
/// construction rather than merely less likely. Keyed on those three
/// class names because QAccessible consults factories per class level,
/// most-derived first, newest factory first: at the "QListView" level
/// this factory is asked before Qt's own, so it wins for every
/// QListWidget, the media table and the dialogs' trees alike.
///
/// COST. VoiceOver announces those views but cannot step through their
/// rows — and that includes combo-box and completer popups, which are
/// QListView subclasses too. Widgets that are not item views keep full
/// accessibility. The views still emit per-row events; the interface here
/// reports no children at all, so the bridge cannot resolve them and says
/// so (rather than resolving a row index to the viewport or a scroll bar
/// and moving the VoiceOver cursor there) — AppLog drops exactly those two
/// messages (see logfile.cpp) so a click does not write a warning to the
/// user's log.
///
/// RETIRES ITSELF. The whole thing compiles to nothing on a Qt that
/// carries the fix (>= 6.6.2) and on Windows. Delete this file when the
/// Qt pin moves past 6.6.2.
namespace MacAccessibilityGuard
{
#if defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
	inline constexpr bool kActive = true;
#else
	inline constexpr bool kActive = false;
#endif

	/// Install the factory. Call once, after QApplication exists, before
	/// any window is shown. A no-op where kActive is false.
	void install();
} // namespace MacAccessibilityGuard
