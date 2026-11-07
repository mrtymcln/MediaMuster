#pragma once

#include "oprequest.h"

struct MediaFile;
/// The scan's safety conclusions and captured identities required for operations.
/// Owns no evidence, parsed source or scan session.
struct OpScanReceipt
{
	OpHeaderIdentity headerIdentity;
	QString databaseFileMobIdToVerify;
};

// Preserve the scan receipt and the Avid identity claims to verify on the opened file.
OpItem opItemFromMediaFile(const MediaFile &file);
