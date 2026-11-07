#pragma once

#include "opfile.h"
#include "oprequest.h"

// Operation verification is independent of scan coordination and file mutation.
// The caller owns the open source and keeps the existing execution checkpoints.
namespace FileCheck
{
	[[nodiscard]] bool sourceMatches(const OpItem &item, OpFile &source, const OpStamp &expected,
					   const OpStamp &current, QString &error);
}
