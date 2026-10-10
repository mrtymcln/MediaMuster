#!/usr/bin/env python3
"""Run full preservation proofs before three fresh-process Release timing pairs."""

import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
COMMON_HASHES = (
    "filesSha256", "schedulingSha256", "issuesSha256", "stateSha256", "callbacksSha256",
)
GRAPH_HASHES = ("sourceGraphsSha256", "projectionsSha256")
COMMON_FIELDS = (
    "roots", "omfScan", "rows", "candidates", "sources", "discoveryIssues",
    "reconciliationIssues", "discoveryComplete", "parsingComplete",
    "reconciliationComplete", "cancelled", "headerReads", "headerSkips",
    "databaseReads", "readReasonCounts", "callbackCounts", "csvSha256",
)
SOURCE_RECEIPT_FIELDS = ("path", "hint", "outcome", "readReason")
SOURCE_PROOF_FIELDS = (
    "sha256", "localGraphSha256", "projectionSha256", "objects", "relationships",
    "properties", "originalValueBytes", "graphSerializedBytes",
)
ORIGINAL_PROOF_FIELDS = (
    "originalGraphEqual", "originalProjectionEqual", "originalGraphSha256",
    "originalProjectionSha256",
)
IMAGE_FIELDS = (
    "capturedBytes", "expectedSize", "acquisitionOutcome", "acquisitionComplete", "diagnostics",
)
MXF_IMAGE_FIELDS = (
    "physicalSize", "acquiredBytes", "capturedBytes", "rangeCapacityBytes", "rangeCount", "valid", "diagnostics",
)
MEMORY_COUNTERS = (
    "residentBytes", "physicalFootprintBytes", "privateBytes", "peakResidentBytes",
)
FULL_COUNT_FIELDS = (
    "sourceObjects", "sourceRelationships", "sourceProperties", "originalValueBytes",
    "originalGraphsAndProjectionsCompared",
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


# Compare archive and native storage through the same MediaEngine readers.
def source_hashes():
    paths = {
        ROOT / "CMakeLists.txt", ROOT / "tests/CMakeLists.txt",
        ROOT / "tests/mediaengine_compare.cpp", ROOT / "tests/mediaenginefingerprint.h",
        Path(__file__).resolve(),
    }
    # MediaEngine's readers/projection/selection and the app adapter are proof inputs.
    paths.update(ROOT.joinpath("src").glob("*.h"))
    paths.update(ROOT.joinpath("src").glob("*.cpp"))
    for directory in (ROOT / "src/mediaengine",):
        paths.update(directory.glob("*.h"))
        paths.update(directory.glob("*.cpp"))
    return {str(path.relative_to(ROOT)): sha256(path) for path in sorted(paths)}


def build_receipt(probe):
    receipt = {"probe": str(probe), "probeSha256": sha256(probe), "sourceSha256": source_hashes()}
    cache = next((parent / "CMakeCache.txt" for parent in probe.parents
                  if (parent / "CMakeCache.txt").is_file()), None)
    if cache is None:
        receipt["configurationEvidence"] = "No CMakeCache.txt found above probe; Release build unverified."
        receipt["releaseConfigurationVerified"] = False
        return receipt
    wanted = {
        "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES", "CMAKE_CXX_COMPILER",
        "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_OSX_ARCHITECTURES",
        "CMAKE_GENERATOR", "CMAKE_HOME_DIRECTORY", "Qt6_DIR", "Qt6Core_DIR",
    }
    settings = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or "=" not in line:
            continue
        name_type, value = line.split("=", 1)
        name = name_type.split(":", 1)[0]
        if name in wanted:
            settings[name] = value
    receipt["cmakeCache"] = str(cache)
    receipt["cmakeCacheSha256"] = sha256(cache)
    receipt["cmakeSettings"] = settings
    relative_parts = probe.relative_to(cache.parent).parts
    release = settings.get("CMAKE_BUILD_TYPE") == "Release" or (
        "Release" in settings.get("CMAKE_CONFIGURATION_TYPES", "").split(";")
        and "Release" in relative_parts
    )
    receipt["releaseConfigurationVerified"] = release
    receipt["configurationEvidence"] = "Recorded CMake cache settings and probe location; no build flags inferred from filenames."
    libraries = {}
    for name in ("libmediamuster_mediaengine_core.a", "libmediamuster_mediaengine.a",
                 "mediamuster_mediaengine_core.lib", "mediamuster_mediaengine.lib"):
        for parent in (cache.parent, probe.parent, cache.parent / "Release"):
            path = parent / name
            if path.is_file():
                libraries[str(path)] = sha256(path)
    receipt["librarySha256"] = libraries
    return receipt


def check_report(report, mode, expected_rows, csv_path):
    require(report.get("verificationPassed") is True and report.get("errors") == [],
            "Probe reported failed verification")
    require(report.get("verificationMode") == mode, "Probe verification mode differs")
    require(report.get("sourceVerificationPerformed") is (mode == "full"),
            "Source verification scope was not explicit")
    require(report.get("rows") == expected_rows, "Physical row count differs from expected count")
    require(all(report.get(field) is True for field in (
        "discoveryComplete", "parsingComplete", "reconciliationComplete",
    )) and report.get("cancelled") is False, "Scan did not complete")
    require(report.get("sourceAndFolderStampsStable") is True, "Input stamps changed within process")
    initial = report["inputStampsBefore"]
    require(initial == report["inputStampsAfterScan"] == report["inputStampsAfterVerification"],
            "Recorded input stamps differ within process")
    require(all(item.get("available") is True for item in initial), "An input stamp was unavailable")
    expected_hashes = set(COMMON_HASHES + (GRAPH_HASHES if mode == "full" else ()))
    require(set(report["hashes"]) == expected_hashes,
            "Expected proof hashes are missing or scan mode claims an omitted graph proof")
    require(len(report["fileDigests"]) == report["rows"], "Per-row digest count differs")
    require(len(report["sourceDigests"]) == report["sources"] == report["candidates"],
            "Per-source receipt count differs")
    require(sha256(csv_path) == report["csvSha256"], "CSV bytes differ from reported hash")
    with csv_path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        header = next(reader)
        count = sum(1 for _ in reader)
    require(count == expected_rows, "CSV physical row count differs")
    require(report["storage"]["unfinishedGraphs"] == 0, "Expanded unfinished source graphs were retained")
    opened = 0
    for source in report["sourceDigests"]:
        if mode == "full":
            require(all(field in source for field in SOURCE_PROOF_FIELDS),
                    f"Missing recursive graph/projection proof: {source['path']}")
            if source["outcome"] != 0:
                opened += 1
                require(source.get("originalGraphEqual") is True
                        and source.get("originalProjectionEqual") is True,
                        f"Direct format reader differs: {source['path']}")
                require(all(field in source for field in ORIGINAL_PROOF_FIELDS),
                        f"Missing direct format-reader proof: {source['path']}")
                require(source["originalGraphSha256"] == source["localGraphSha256"],
                        f"Original graph digest differs: {source['path']}")
        else:
            require(not any(field in source for field in SOURCE_PROOF_FIELDS + ORIGINAL_PROOF_FIELDS),
                    f"Scan mode claims omitted source graph verification: {source['path']}")
    if mode == "full":
        require(report["originalGraphsAndProjectionsCompared"] == opened,
                "Direct format-reader proof count differs from opened source count")
    else:
        require(not any(field in report for field in FULL_COUNT_FIELDS),
                "Scan mode reports unmeasured source graph counts")
    if report["engine"] == "native":
        require(report["storage"]["databaseArchives"] == 0, "MediaEngine retained full database archives")
        databases = [source for source in report["sourceDigests"]
                     if source["hint"] in (0, 1) and source["outcome"] != 0]
        require(report["storage"]["nativeDatabaseImages"] == len(databases),
                "MediaEngine native database image count differs from opened databases")
        for source in databases:
            image = source.get("databaseImage", {})
            require(image.get("acquisitionComplete") is True
                    and image.get("capturedBytes") == image.get("expectedSize"),
                    f"Native database acquisition was incomplete: {source['path']}")
            if mode == "full":
                require(image.get("exactOriginalBytes") is True
                        and image.get("originalFileSha256") == image.get("imageSha256"),
                        f"Native image differs from exact original bytes: {source['path']}")
            else:
                require(not any(field in image for field in (
                    "exactOriginalBytes", "originalFileSha256", "imageSha256",
                )), f"Scan mode claims omitted original-byte verification: {source['path']}")
        mxf_sources = [source for source in report["sourceDigests"] if "mxfImage" in source]
        storage = report["storage"]
        require(storage["nativeMxfImages"] == len(mxf_sources),
                "MediaEngine native MXF image count differs from reported source images")
        require(storage["nativeMxfImageBytes"] == sum(source["mxfImage"]["capturedBytes"] for source in mxf_sources),
                "MediaEngine native MXF byte total differs from source images")
        require(storage["nativeMxfAcquiredBytes"] == sum(source["mxfImage"]["acquiredBytes"] for source in mxf_sources),
                "MediaEngine native MXF acquisition total differs from source images")
        require(storage["nativeMxfImageRanges"] == sum(source["mxfImage"]["rangeCount"] for source in mxf_sources),
                "MediaEngine native MXF range total differs from source images")
        require(storage["nativeMxfRangeCapacityBytes"] == sum(source["mxfImage"]["rangeCapacityBytes"] for source in mxf_sources),
                "MediaEngine native MXF array capacity total differs from source images")
        require(storage["nativeDatabaseImageBytes"] == storage["nativeImageBytes"],
                "Explicit database byte total differs from historical report alias")
        for source in mxf_sources:
            image = source["mxfImage"]
            require(source["hint"] == 2 and source["outcome"] == 1,
                    f"Native MXF image published for a non-complete MXF: {source['path']}")
            require(all(field in image for field in MXF_IMAGE_FIELDS)
                    and image["valid"] is True
                    and 0 <= image["capturedBytes"] <= image["acquiredBytes"]
                    and image["capturedBytes"] <= image["rangeCapacityBytes"]
                    and image["physicalSize"] >= image["capturedBytes"],
                    f"Native MXF capture receipt was invalid: {source['path']}")
            if mode == "full":
                require(image.get("exactOriginalRanges") is True
                        and image.get("rangesWellFormed") is True
                        and image["originalRangeSha256"] == image["imageRangeSha256"],
                        f"Native MXF ranges differ from original bytes: {source['path']}")
                require(len(image["ranges"]) == image["rangeCount"]
                        and sum(region["bytes"] for region in image["ranges"]) == image["capturedBytes"],
                        f"Native MXF range proof count/bytes differ: {source['path']}")
                end = 0
                for region in image["ranges"]:
                    require(region["offset"] >= end and region["bytes"] > 0
                            and region["offset"] + region["bytes"] <= image["physicalSize"],
                            f"Native MXF range proof has invalid original extents: {source['path']}")
                    require(region["exactOriginalBytes"] is True
                            and region["originalSha256"] == region["imageSha256"],
                            f"Native MXF range differs at {region['offset']}: {source['path']}")
                    end = region["offset"] + region["bytes"]
            else:
                require(not any(field in image for field in (
                    "exactOriginalRanges", "rangesWellFormed", "originalRangeSha256", "imageRangeSha256", "ranges",
                )), f"Scan mode claims omitted MXF-range verification: {source['path']}")
    return {"csvRows": count, "csvHeader": header}


def compare_reports(baseline, current, full):
    for field in COMMON_FIELDS:
        require(baseline[field] == current[field], f"Semantic report field differs: {field}")
    hashes = COMMON_HASHES + (GRAPH_HASHES if full else ())
    for name in hashes:
        require(baseline["hashes"][name] == current["hashes"][name], f"Semantic hash differs: {name}")
    require(baseline["fileDigests"] == current["fileDigests"], "Per-row semantic hashes differ")
    require(baseline["inputStampsBefore"] == current["inputStampsBefore"],
            "Inputs changed between fresh processes")
    for reference, source in zip(baseline["sourceDigests"], current["sourceDigests"]):
        fields = SOURCE_RECEIPT_FIELDS + (SOURCE_PROOF_FIELDS if full else ())
        if full and source["outcome"] != 0:
            fields += ORIGINAL_PROOF_FIELDS
        for field in fields:
            require(reference[field] == source[field], f"Source {field} differs: {source['path']}")
    if full:
        for field in FULL_COUNT_FIELDS:
            require(baseline[field] == current[field], f"Full graph count differs: {field}")


def performance_summary(runs):
    measured = [run for run in runs if run["verificationMode"] == "scan"]
    metrics = {}
    for engine in ("archive", "native"):
        group = [run for run in measured if run["engine"] == engine]
        if not group:
            continue
        values = {"scanMs": [run["scanMs"] for run in group]}
        for counter in MEMORY_COUNTERS:
            if all(counter in run["memoryRetained"] for run in group):
                values[counter] = [run["memoryRetained"][counter] for run in group]
        metrics[engine] = {name: {"values": series, "median": statistics.median(series),
                                  "min": min(series), "max": max(series)}
                           for name, series in values.items()}
    changes = {}
    if len(metrics) == 2:
        for name in metrics["archive"].keys() & metrics["native"].keys():
            before, after = metrics["archive"][name]["median"], metrics["native"][name]["median"]
            changes[name] = {"archiveMedian": before, "nativeMedian": after,
                             "nativeMinusArchive": after - before,
                             "changePercent": 100 * (after / before - 1) if before else None,
                             "reductionPercent": 100 * (1 - after / before) if before else None}
    return {"engines": metrics, "medianChanges": changes,
            "definition": "Medians use only three scan-mode trials per storage mode. Negative changePercent means native storage uses less time/memory than archive storage. Current and peak process counters are sampled by the probe before verification/CSV; absent counters are not treated as zero."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-rows", type=int, required=True)
    parser.add_argument("roots", nargs="+")
    args = parser.parse_args()
    probe = args.probe.resolve(strict=True)
    output = args.output_dir.resolve()
    if args.expected_rows < 0:
        parser.error("--expected-rows must be nonnegative")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error("--output-dir must be new or empty; existing evidence is preserved")
    output.mkdir(parents=True, exist_ok=True)
    csv.field_size_limit(sys.maxsize)
    receipt = build_receipt(probe)
    summary = {
        "complete": False, "status": "running", "roots": args.roots,
        "expectedRows": args.expected_rows, "buildReceipt": receipt,
        "host": {"platform": platform.platform(), "machine": platform.machine(),
                 "python": sys.version},
        "environmentOverrides": {"QT_HASH_SEED": "0"},
        "method": "Eight sequential fresh processes using one Release probe for both MediaEngine storage modes: full archive storage then full native storage; three scan-only pairs ordered archive/native, native/archive, archive/native. Filesystem cache is uncontrolled. No Windows/NEXIS or cold-cache claim follows from a macOS run.",
        "proofScope": {"full": "Complete recursive graphs, alias topology, projections, direct format-reader equality, exact MediaEngine native database bytes and original-offset/byte equality for every retained MXF range, plus scan/CSV evidence.",
                       "scan": "Final row/evidence, scheduling, issues, callbacks, completion, CSV and unchanged input stamps. Source graph/projection/original-byte verification is omitted.",
                       "storage": "Representation differences are reported separately. Native storage requires zero database archives and unfinished graphs. Complete, valid MXF reads retain native acquired ranges; non-complete/invalid MXF reads and legacy media keep graph archives. Acquired bytes include rereads; captured bytes count unique retained positions. No picture/sound payload retention policy is changed."},
        "runs": [], "comparisons": [],
    }
    write_json(output / "build-receipt.json", receipt)
    write_json(output / "summary.json", summary)
    full_reports = {}
    order = [("full", 0, "archive"), ("full", 0, "native")]
    order += [("scan", pair, engine) for pair, engines in (
        (1, ("archive", "native")), (2, ("native", "archive")), (3, ("archive", "native")),
    ) for engine in engines]
    try:
        require(receipt["releaseConfigurationVerified"], "Probe Release build could not be verified from CMake cache")
        for number, (mode, pair, engine) in enumerate(order, 1):
            name = f"{number:02d}-{mode}-pair{pair}-{engine}"
            report_path, csv_path = output / f"{name}.json", output / f"{name}.csv"
            command = [str(probe), "--engine", engine, "--output", str(report_path),
                       "--csv", str(csv_path), "--expected-rows", str(args.expected_rows)]
            if mode == "scan":
                command.append("--measure-only")
            command += args.roots
            run = {"name": name, "engine": engine, "verificationMode": mode, "pair": pair,
                   "command": command, "report": str(report_path), "csv": str(csv_path),
                   "stdout": str(output / f"{name}.stdout.txt"),
                   "stderr": str(output / f"{name}.stderr.txt"),
                   "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
            summary["runs"].append(run)
            write_json(output / "summary.json", summary)
            print(json.dumps({"event": "started", "run": name}), flush=True)
            started = time.perf_counter()
            with Path(run["stdout"]).open("wb") as stdout, Path(run["stderr"]).open("wb") as stderr:
                process = subprocess.run(command, cwd=ROOT, env=dict(os.environ, QT_HASH_SEED="0"),
                                         stdout=stdout, stderr=stderr)
            run["returncode"] = process.returncode
            run["processWallSeconds"] = time.perf_counter() - started
            write_json(output / "summary.json", summary)
            require(process.returncode == 0, f"{name}: probe exited {process.returncode}; see retained report/stdout/stderr")
            report = json.loads(report_path.read_text(encoding="utf-8"))
            require(report.get("engine") == engine, "Probe report names a different engine")
            csv_receipt = check_report(report, mode, args.expected_rows, csv_path)
            run.update(csv_receipt)
            run.update({key: report[key] for key in (
                "scanMs", "verificationMs", "memoryBefore", "memoryRetained", "rows",
                "sources", "headerReads", "headerSkips", "databaseReads", "readReasonCounts", "storage",
            )})
            run["csvSha256"] = sha256(csv_path)
            run["reportSha256"] = sha256(report_path)
            require(sha256(probe) == receipt["probeSha256"], "Probe binary changed during comparison")
            if mode == "full":
                full_reports[engine] = report
                if engine == "native":
                    compare_reports(full_reports["archive"], report, full=True)
                    require(summary["runs"][0]["csvHeader"] == csv_receipt["csvHeader"], "CSV headings differ")
                    summary["comparisons"].append({"run": name, "baseline": summary["runs"][0]["name"],
                                                   "scope": "full", "passed": True})
                    summary["fullProofPassed"] = True
                    summary["fullGraphProof"] = {
                        "passed": True, "sources": report["sources"],
                        "directOriginalSourcesCompared": report["originalGraphsAndProjectionsCompared"],
                        "objects": report["sourceObjects"], "relationships": report["sourceRelationships"],
                        "properties": report["sourceProperties"], "originalValueBytes": report["originalValueBytes"],
                        "hashes": {key: report["hashes"][key] for key in GRAPH_HASHES},
                    }
                    summary["nativeDatabaseByteProof"] = {
                        "passed": True, "images": report["storage"]["nativeDatabaseImages"],
                        "bytes": report["storage"]["nativeImageBytes"],
                        "scope": "Every opened MediaEngine PMR/MDB image directly compared byte-for-byte with its unchanged original file in the full run. Timing trials do not repeat this proof.",
                    }
                    summary["nativeMxfByteProof"] = {
                        "passed": True, "images": report["storage"]["nativeMxfImages"],
                        "bytes": report["storage"]["nativeMxfImageBytes"],
                        "acquiredBytes": report["storage"]["nativeMxfAcquiredBytes"],
                        "rangeCapacityBytes": report["storage"]["nativeMxfRangeCapacityBytes"],
                        "ranges": report["storage"]["nativeMxfImageRanges"],
                        "completeArchiveFallbacks": report["storage"]["completeMxfArchiveFallbacks"],
                        "scope": "Every retained MediaEngine MXF byte range directly compared with its unchanged original at the same original offset in the full run. Range hashes frame original offsets and lengths. Recording payloads outside those acquired ranges are not read for this proof. Timing trials do not repeat this proof.",
                    }
            else:
                compare_reports(full_reports["archive"], report, full=False)
                own_baseline = full_reports[engine]
                require(own_baseline["storage"] == report["storage"], f"{engine} retained storage totals changed")
                for reference, source in zip(own_baseline["sourceDigests"], report["sourceDigests"]):
                    if "databaseImage" in reference:
                        for field in IMAGE_FIELDS:
                            require(reference["databaseImage"][field] == source["databaseImage"][field],
                                    f"Native acquisition {field} changed: {source['path']}")
                    if "mxfImage" in reference:
                        for field in MXF_IMAGE_FIELDS:
                            require(reference["mxfImage"][field] == source["mxfImage"][field],
                                    f"Native MXF acquisition {field} changed: {source['path']}")
                require(summary["runs"][0]["csvHeader"] == csv_receipt["csvHeader"], "CSV headings differ")
                summary["comparisons"].append({"run": name, "baseline": summary["runs"][0]["name"],
                                               "scope": "scan", "passed": True})
            run["checksPassed"] = True
            summary["performance"] = performance_summary(summary["runs"])
            write_json(output / "summary.json", summary)
            print(json.dumps({"event": "completed", "run": name, "scanMs": report["scanMs"],
                              "rows": report["rows"], "checksPassed": True}), flush=True)
        require(source_hashes() == receipt["sourceSha256"], "Proof source files changed during comparison")
        summary["complete"] = True
        summary["status"] = "passed"
        summary["fullProofPassed"] = True
        summary["timingPairsPassed"] = 3
        summary["performance"] = performance_summary(summary["runs"])
        write_json(output / "summary.json", summary)
        print(json.dumps({"event": "all-complete", "summary": str(output / "summary.json")}), flush=True)
        return 0
    except Exception as error:
        summary["status"] = "failed"
        summary["failure"] = {"type": type(error).__name__, "message": str(error)}
        write_json(output / "summary.json", summary)
        print(f"Comparison failed: {error}\nEvidence retained in {output}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
