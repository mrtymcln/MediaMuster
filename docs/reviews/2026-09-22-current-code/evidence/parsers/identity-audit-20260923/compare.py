#!/usr/bin/env python3
"""Compare MXF identity ledgers; use fixture-relative paths from probe.cpp."""
import json
import sys

FIELDS = (
    "fileId", "masterId", "fileZero", "masterZero", "headerStatus", "valid",
    "hasMaterialPackage", "classificationKnown", "clipName", "sha256", "size",
)


def read_mxf(path):
    with open(path, encoding="utf-8") as stream:
        rows = [json.loads(line) for line in stream]
    # Older probe output given a relative input directory retained this prefix.
    return {row["path"].removeprefix("tests/fixtures/"): row
            for row in rows if row["kind"] == "mxf"}


before, after = (read_mxf(path) for path in sys.argv[1:3])
changes = []
for path in sorted(before.keys() & after.keys()):
    differences = {key: {"baseline": before[path].get(key),
                         "current": after[path].get(key)}
                   for key in FIELDS if before[path].get(key) != after[path].get(key)}
    if differences:
        changes.append({"path": path, "changes": differences})
report = {"baseline_mxf_paths": len(before), "current_mxf_paths": len(after),
          "compared_records": len(before.keys() & after.keys()),
          "only_baseline": sorted(before.keys() - after.keys()),
          "only_current": sorted(after.keys() - before.keys()),
          "changed_records": len(changes), "changes": changes}
print(json.dumps(report, indent=2))
sys.exit(bool(changes or before.keys() != after.keys()))
