#pragma once

// Presentation boundary for the existing table, CSV and file-operation model.
// Matching and selection happen inside Canon; this only formats chosen facts.
#include "mediafile.h"
#include "canon/scanmodel.h"

MediaFile canonMediaFile(const Canon::MediaFile &file,
						 const QSharedPointer<const Canon::ScanResult> &scan);

// Refresh only semantic fields from current selections. Physical row identity,
// filesystem/volume/family details and operation stamps remain unchanged.
// Returns whether any semantic row value changed.
bool applyResolvedMetadata(MediaFile &file);
