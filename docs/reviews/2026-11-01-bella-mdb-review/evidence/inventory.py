#!/usr/bin/env python3
"""Inventory filename-suffixed MDBs without following directory aliases."""
import datetime
import hashlib
import json
import os
from pathlib import Path

OUT = Path(__file__).resolve().parent
errors = []
files = []
skipped = []
roots = ["/", "/Volumes/EDIT"]
# Data is already visible at /. Avoid traversing its firmlink aliases twice.
excluded = {"/dev", "/Volumes", "/System/Volumes", "/Network", "/net", "/cores"}

def error(exc):
    errors.append({"path": exc.filename, "error": str(exc)})

for root in roots:
    for folder, dirs, names in os.walk(root, topdown=True, onerror=error, followlinks=False):
        retained = []
        for name in dirs:
            path = os.path.join(folder, name)
            if path in excluded or os.path.islink(path):
                skipped.append({"path": path, "reason": "alias or excluded virtual/duplicate volume"})
            else:
                retained.append(name)
        dirs[:] = retained
        for name in names:
            if not name.lower().endswith(".mdb"):
                continue
            path = os.path.join(folder, name)
            item = {"path": path, "root": root}
            try:
                before = os.stat(path)
                digest = hashlib.sha256()
                with open(path, "rb") as stream:
                    while block := stream.read(1024 * 1024):
                        digest.update(block)
                    stream.seek(max(0, before.st_size - 24))
                    item["footerHex"] = stream.read().hex()
                after = os.stat(path)
                item.update(size=after.st_size, mtimeNs=after.st_mtime_ns,
                            device=after.st_dev, inode=after.st_ino,
                            sha256=digest.hexdigest(),
                            stable=(before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns))
            except OSError as exc:
                item["error"] = str(exc)
            files.append(item)

result = {"timestampUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "roots": roots, "directoryAliasesFollowed": False,
          "excluded": sorted(excluded), "files": sorted(files, key=lambda x: x["path"]),
          "errors": errors, "skipped": skipped}
(OUT / "inventory.json").write_text(json.dumps(result, indent=2) + "\n")
(OUT / "mdb-paths.txt").write_text("".join(x["path"] + "\n" for x in result["files"]))
print(json.dumps({"paths": len(files), "uniqueHashes": len({x.get("sha256") for x in files if x.get("sha256")}),
                  "walkErrors": len(errors), "fileErrors": sum("error" in x for x in files),
                  "byRoot": {r: sum(x["root"] == r for x in files) for r in roots}}))
