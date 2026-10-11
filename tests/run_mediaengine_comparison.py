#!/usr/bin/env python3
"""Validate metadata/evidence and profile three fresh-process Release scans."""

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
COMMON_FIELDS = (
    "roots", "omfScan", "rows", "candidates", "sources", "discoveryIssues",
    "reconciliationIssues", "discoveryComplete", "parsingComplete",
    "reconciliationComplete", "cancelled", "headerReads", "headerSkips",
    "databaseReads", "readReasonCounts", "callbackCounts", "csvSha256",
)
SOURCE_RECEIPT_FIELDS = ("path", "hint", "outcome", "readReason")
MEMORY_COUNTERS = (
    "residentBytes", "physicalFootprintBytes", "privateBytes", "peakResidentBytes",
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


# Record the exact source inputs to the probe and app-boundary proof.
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


def check_report(report, expected_rows, csv_path):
    require(report.get("verificationPassed") is True and report.get("errors") == [],
            "Probe reported failed verification")
    require(report.get("verificationMode") == "scan"
            and report.get("sourceVerificationPerformed") is False,
            "Probe verification scope was not explicit")
    require(report.get("rows") == expected_rows, "Physical row count differs from expected count")
    require(all(report.get(field) is True for field in (
        "discoveryComplete", "parsingComplete", "reconciliationComplete",
    )) and report.get("cancelled") is False, "Scan did not complete")
    require(report.get("sourceAndFolderStampsStable") is True, "Input stamps changed within process")
    initial = report["inputStampsBefore"]
    require(initial == report["inputStampsAfterScan"] == report["inputStampsAfterVerification"],
            "Recorded input stamps differ within process")
    require(all(item.get("available") is True for item in initial), "An input stamp was unavailable")
    require(set(report["hashes"]) == set(COMMON_HASHES), "Expected scan proof hashes differ")
    require(len(report["fileDigests"]) == report["rows"], "Per-row digest count differs")
    require(len(report["sourceDigests"]) == report["sources"] == report["candidates"],
            "Per-source receipt count differs")
    require(all(all(field in source for field in SOURCE_RECEIPT_FIELDS)
                for source in report["sourceDigests"]), "A source receipt is incomplete")
    require(sha256(csv_path) == report["csvSha256"], "CSV bytes differ from reported hash")
    with Path(csv_path).open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        header = next(reader)
        count = sum(1 for _ in reader)
    require(count == expected_rows, "CSV physical row count differs")
    return {"csvRows": count, "csvHeader": header}


def compare_reports(baseline, current):
    for field in COMMON_FIELDS:
        require(baseline[field] == current[field], f"Semantic report field differs: {field}")
    for name in COMMON_HASHES:
        require(baseline["hashes"][name] == current["hashes"][name], f"Semantic hash differs: {name}")
    require(baseline["fileDigests"] == current["fileDigests"], "Per-row semantic hashes differ")
    require(baseline["inputStampsBefore"] == current["inputStampsBefore"],
            "Inputs changed between fresh processes")
    require(len(baseline["sourceDigests"]) == len(current["sourceDigests"]),
            "Source receipt counts differ")
    for reference, source in zip(baseline["sourceDigests"], current["sourceDigests"]):
        for field in SOURCE_RECEIPT_FIELDS:
            require(reference[field] == source[field], f"Source {field} differs: {source['path']}")


def performance_summary(runs):
    values = {"scanMs": [run["scanMs"] for run in runs]}
    for counter in MEMORY_COUNTERS:
        if all(counter in run["memoryRetained"] for run in runs):
            values[counter] = [run["memoryRetained"][counter] for run in runs]
    return {
        "metrics": {name: {"values": series, "median": statistics.median(series),
                           "min": min(series), "max": max(series)}
                    for name, series in values.items()},
        "definition": "Fresh-process scan medians. Current and peak process counters are sampled by the probe before evidence fingerprints/CSV; absent counters are not treated as zero.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-rows", type=int, required=True)
    parser.add_argument("--no-omf", action="store_true", help="Disable OMF-family discovery")
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
        "omfScan": not args.no_omf,
        "expectedRows": args.expected_rows, "buildReceipt": receipt,
        "host": {"platform": platform.platform(), "machine": platform.machine(),
                 "python": sys.version},
        "environmentOverrides": {"QT_HASH_SEED": "0"},
        "method": "Three sequential fresh processes using one verified Release probe and the normal MediaEngine. Every run validates scan metadata/evidence and CSV; subsequent runs must agree with the first. Filesystem cache is uncontrolled. No Windows/NEXIS or cold-cache claim follows from a macOS run.",
        "proofScope": "Final row properties, read coverage, observations, alternatives, selections and receipt aliases; scheduling, issues, callbacks, completion, CSV and unchanged input stamps. Dedicated format-reader tests verify parsed records separately.",
        "runs": [], "comparisons": [],
    }
    write_json(output / "build-receipt.json", receipt)
    write_json(output / "summary.json", summary)
    baseline = None
    try:
        require(receipt["releaseConfigurationVerified"], "Probe Release build could not be verified from CMake cache")
        for number in range(1, 4):
            name = f"scan-{number:02d}"
            report_path, csv_path = output / f"{name}.json", output / f"{name}.csv"
            command = [str(probe), "--output", str(report_path), "--csv", str(csv_path),
                       "--expected-rows", str(args.expected_rows)]
            if args.no_omf:
                command.append("--no-omf")
            command += args.roots
            run = {
                "name": name, "command": command, "report": str(report_path), "csv": str(csv_path),
                "stdout": str(output / f"{name}.stdout.txt"),
                "stderr": str(output / f"{name}.stderr.txt"),
                "startedUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            }
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
            csv_receipt = check_report(report, args.expected_rows, csv_path)
            run.update(csv_receipt)
            run.update({key: report[key] for key in (
                "scanMs", "verificationMs", "memoryBefore", "memoryRetained", "rows", "sources",
                "headerReads", "headerSkips", "databaseReads", "readReasonCounts",
            )})
            run["csvSha256"] = sha256(csv_path)
            run["reportSha256"] = sha256(report_path)
            require(sha256(probe) == receipt["probeSha256"], "Probe binary changed during profiling")
            if baseline is None:
                baseline = report
            else:
                compare_reports(baseline, report)
                require(summary["runs"][0]["csvHeader"] == csv_receipt["csvHeader"], "CSV headings differ")
                summary["comparisons"].append({"run": name, "baseline": summary["runs"][0]["name"],
                                               "passed": True})
            run["checksPassed"] = True
            summary["performance"] = performance_summary(summary["runs"])
            write_json(output / "summary.json", summary)
            print(json.dumps({"event": "completed", "run": name, "scanMs": report["scanMs"],
                              "rows": report["rows"], "checksPassed": True}), flush=True)
        require(source_hashes() == receipt["sourceSha256"], "Proof source files changed during profiling")
        summary["complete"] = True
        summary["status"] = "passed"
        summary["scansPassed"] = len(summary["runs"])
        summary["performance"] = performance_summary(summary["runs"])
        write_json(output / "summary.json", summary)
        print(json.dumps({"event": "all-complete", "summary": str(output / "summary.json")}), flush=True)
        return 0
    except Exception as error:
        summary["status"] = "failed"
        summary["failure"] = {"type": type(error).__name__, "message": str(error)}
        write_json(output / "summary.json", summary)
        print(f"Scan proof failed: {error}\nEvidence retained in {output}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
