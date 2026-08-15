#pragma once

#include "rebalanceplan.h"

/// Each returns a self-consistent plan that renders correctly
/// in RebalanceDialog without touching disk.
namespace RebalanceDemo
{

	/// Small consolidation: ~50 files between 4 folders.
	RebalancePlan small();

	/// Big consolidation: ~8,000 files, mix of red/amber bars,
	/// 3 new folders allocated to absorb overflow.
	RebalancePlan big();

	/// 666,666 files redistributed across hundreds of folders.
	/// Stress test for the rendering path; also fun to watch.
	RebalancePlan reallyBig();

} // namespace RebalanceDemo