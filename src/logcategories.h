#pragma once

#include <QLoggingCategory>

/// Qt logging categories for developer telemetry. The shipped app forces
/// every category to full debug detail in AppLog::install(), so nothing
/// needs enabling there. Outside that (unit tests, standalone runs) they
/// default to warnings and up; raise one with, e.g.,
///   QT_LOGGING_RULES="mediamuster.scanner.debug=true"
///
/// The in-app console (scanLogBatch, operationLog, Rebalancer::log)
/// is a separate signal-based channel and stays out of here.

Q_DECLARE_LOGGING_CATEGORY(lcAvb)
Q_DECLARE_LOGGING_CATEGORY(lcBento)
Q_DECLARE_LOGGING_CATEGORY(lcMdb)
Q_DECLARE_LOGGING_CATEGORY(lcMxf)
Q_DECLARE_LOGGING_CATEGORY(lcOmf) // OMF-era: the legacy essence reader logs here, apart from lcMxf
Q_DECLARE_LOGGING_CATEGORY(lcPmr)
Q_DECLARE_LOGGING_CATEGORY(lcScanner)
Q_DECLARE_LOGGING_CATEGORY(lcVolume)
Q_DECLARE_LOGGING_CATEGORY(lcWorker)