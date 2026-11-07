#pragma once

struct MediaFile;
namespace Bella
{
    struct ScanEntry;
}

// Prepare one app row from the completed Bella scan. Does not read the file.
[[nodiscard]] MediaFile bellaMediaFile(const Bella::ScanEntry &entry);
