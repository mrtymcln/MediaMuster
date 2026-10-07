#pragma once

#include "oprequest.h"

struct MediaFile;

// Preserve the scan receipt and the Avid identity claims to verify on the opened file.
OpItem opItemFromMediaFile(const MediaFile &file);
