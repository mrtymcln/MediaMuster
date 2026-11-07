#pragma once

struct MediaFile;
namespace Bella
{
    struct ScanEntry;
}

// Populate the existing row fields consumed by opItemFromMediaFile.
void applyBellaOperationHandoff(MediaFile &row, const Bella::ScanEntry &entry);
