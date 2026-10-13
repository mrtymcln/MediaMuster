#pragma once

#include "oprequest.h"
#include <QHash>

struct MediaFile;
namespace MediaEngine
{
	struct ScanResult;
}

/// The scan's safety conclusions; owns no evidence, parsed source or scan session.
struct OpScanReceipt
{
	OpHeaderIdentity headerIdentity;
	QString databaseFileMobIdToVerify;
};

/// Temporary path lookup shared while capturing a completed scan's row receipts.
class OpScanReceiptLookup
{
public:
	OpScanReceiptLookup() = default;
	explicit OpScanReceiptLookup(const MediaEngine::ScanResult &scan);
	bool headerWasSkipped(const QString &path) const;
	bool sourceChanged(const QString &path) const;

private:
	struct Flags
	{
		bool headerSkipped = false;
		bool sourceChanged = false;
	};
	QHash<QString, Flags> m_flags;
};

void captureOpScanReceipt(MediaFile &file, const OpScanReceiptLookup &lookup);
void captureOpScanReceipt(MediaFile &file);

// Preserve the scan receipt and the Avid identity claims to verify on the opened file.
OpItem opItemFromMediaFile(const MediaFile &file);
