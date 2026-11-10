#pragma once

#include "operation_file.h"
#include "operation_request.h"

// Operation verification is independent of scan coordination and file mutation.
// The caller owns the open source and keeps the existing execution checkpoints.
// MARK: - File validation

namespace FileCheck
{
	// Compare an existing observation without making another filesystem request.
	[[nodiscard]] bool unchanged(const OperationStamp &expected, const OperationStamp &observed);
	[[nodiscard]] bool unchanged(const OperationStamp &expected, const QString &path);
	[[nodiscard]] bool unchanged(const OperationStamp &expected, const OperationFile &opened, const QString &path);
	// Partial copies and directories may change size/date while remaining the
	// same object. Keep that question separate from an unchanged-file check.
	[[nodiscard]] bool sameObject(const OperationStamp &expected, const OperationStamp &observed);
	[[nodiscard]] bool hasSize(const OperationStamp &observed, qint64 expectedBytes);
	[[nodiscard]] bool completedCopyMatches(const QString &path, const OperationStamp &expected, QString &error);

	[[nodiscard]] bool sourceMatches(const OperationItem &item, OperationFile &source, const OperationStamp &expected,
									 const OperationStamp &current, QString &error);
}
