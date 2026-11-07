#pragma once

// Presentation boundary for legacy scan rows, CSV and frozen operation checks.
// Normal Bella scans prepare rows through bellarowpresentation instead.
// Matching and selection happen inside MediaEngine; this formats chosen facts
// and captures the operation safety checks needed after the scan is released.
#include "mediafile.h"
#include "mediaengine/scanmodel.h"

MediaFile mediaEngineMediaFile(const MediaEngine::MediaFile &file,
						 const ScanRowLookup &lookup,
						 const QString &volumePath = {}, const QString &volumeName = {},
						 bool cancelled = false);

// Format semantic fields from borrowed selected evidence. Physical row identity,
// filesystem/volume/family details and operation stamps remain unchanged.
// Returns whether any semantic row value changed.
bool applyResolvedMetadata(MediaFile &file, const MediaEvidence &evidence);
