#pragma once

struct MediaFileRecord;
namespace Bella
{
	struct ScanEntry;
}

// Build a complete inventory record from the facts Bella already collected.
[[nodiscard]] MediaFileRecord mediaFileRecordFromScanEntry(const Bella::ScanEntry &entry);
