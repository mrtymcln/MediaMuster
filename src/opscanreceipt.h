#pragma once

#include "oprequest.h"
#include "mediametadatastatus.h"
#include <QHash>

struct MediaFile;
namespace MediaEngine
{
	struct ScanResult;
}

/// The scan's safety conclusions, including all established header identities.
/// Owns no evidence, parsed source or scan session.
struct OpScanReceipt
{
	OpHeaderIdentity headerIdentity;
	QString databaseFileMobIdToVerify;
};

/// Scoped read outcomes and change flags used while preparing completed scan rows.
class ScanRowLookup
{
public:
	ScanRowLookup() = default;
	explicit ScanRowLookup(const MediaEngine::ScanResult &scan);
	bool headerWasSkipped(const QString &path) const;
	bool sourceChanged(const QString &path) const;
	MediaHeaderReadOutcome headerOutcome(const QString &path) const;

private:
	struct Flags
	{
		bool headerSkipped = false;
		bool sourceChanged = false;
		MediaHeaderReadOutcome headerOutcome = MediaHeaderReadOutcome::NoReceipt;
	};
	QHash<QString, Flags> m_flags;
};

OpScanReceipt makeOpScanReceipt(const MediaEvidence &evidence, const MediaScanStamp &stamp,
							   const ScanRowLookup &lookup = {});

// Preserve the scan receipt and the Avid identity claims to verify on the opened file.
OpItem opItemFromMediaFile(const MediaFile &file);
