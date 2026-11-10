#pragma once

struct MediaFile;
namespace Bella
{
    struct ScanEntry;
}

// MARK: - File identities and operation safety

// Populate the existing row fields consumed by opItemFromMediaFile.
void applyBellaOperationHandoff(MediaFile &row, const Bella::ScanEntry &entry);

// MARK: - Prepared media row

// Prepare one app row from the completed Bella scan. Does not read the file.
[[nodiscard]] MediaFile bellaMediaFile(const Bella::ScanEntry &entry);
