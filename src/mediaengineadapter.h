#pragma once

// Presentation boundary for the existing table, CSV and file-operation model.
// Matching and selection happen inside MediaEngine; this formats chosen facts
// and captures the operation safety checks needed after the scan is released.
#include "mediafile.h"
#include "mediaengine/scanmodel.h"

MediaFile mediaEngineMediaFile(const MediaEngine::MediaFile &file,
						 const QSharedPointer<const MediaEngine::ScanResult> &scan,
						 const QString &volumePath = {}, const QString &volumeName = {},
						 const OpScanReceiptLookup *operationChecks = nullptr);

// Refresh only semantic fields from current selections. Physical row identity,
// filesystem/volume/family details and operation stamps remain unchanged.
// Returns whether any semantic row value changed.
bool applyResolvedMetadata(MediaFile &file);
