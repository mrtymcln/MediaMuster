from pathlib import Path
import datetime
import hashlib
import json
import os
import platform
import statistics
import subprocess
import time

ROOT = Path('/Users/martymclean/Developer/MediaMuster')
OUT = Path('/private/tmp/mdvx-performance-audit/measurements')
RELEASE = Path('/private/tmp/mediamuster-release-performance-20261009/build/tests/tst_scanner')
DEBUG = ROOT / 'build/tests/tst_scanner'
ROOTS = ['/Users/Shared/AvidMediaComposer', '/Volumes/EDIT']
ORDER = [(1, 'debug'), (1, 'release'), (2, 'release'), (2, 'debug'), (3, 'debug'), (3, 'release')]
REFERENCE = json.loads((ROOT / 'Project Canon/evidence/source-archive-after-real-scan-2026-10-09.json').read_text())
SOURCE_PATHS = sorted({source['path'] for source in REFERENCE['sources']})
FOLDERS = sorted({str(Path(path).parent) for path in SOURCE_PATHS})


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command_output(command):
    process = subprocess.run(command, capture_output=True, text=True)
    return {'command': command, 'returncode': process.returncode,
            'stdout': process.stdout.strip(), 'stderr': process.stderr.strip()}


def stamps():
    result = []
    for path in SOURCE_PATHS + FOLDERS:
        value = os.stat(path)
        result.append({'path': path, 'device': str(value.st_dev), 'inode': str(value.st_ino),
                       'size': str(value.st_size), 'mtimeNs': str(value.st_mtime_ns),
                       'directory': path in FOLDERS})
    return result


def semantic_sources(report):
    # Allocation capacity is excluded from the previous preservation proof.
    return [{key: value for key, value in source.items() if key != 'propertyCapacity'}
            for source in report['sources']]


def checks_against(a, b, a_csv, b_csv):
    return {
        'roots': a['roots'] == b['roots'],
        'rows': a['rows'] == b['rows'],
        'issueCount': a['issueCount'] == b['issueCount'],
        'inventoryEveryRow': a['inventory'] == b['inventory'],
        'issuesEveryEntry': a['issues'] == b['issues'],
        'sourceOrderStatusReadReasonArchivesAndGraphCounts': semantic_sources(a) == semantic_sources(b),
        'propertyAllocationCapacities': [s['propertyCapacity'] for s in a['sources']] ==
                                       [s['propertyCapacity'] for s in b['sources']],
        'csvByteIdentical': Path(a_csv).read_bytes() == Path(b_csv).read_bytes(),
    }


def summarize(results):
    paired = []
    for pair in (1, 2, 3):
        group = {r['mode']: r for r in results if r['pair'] == pair}
        if len(group) != 2:
            continue
        a, b = group['debug'], group['release']
        a_report = json.loads(Path(a['report']).read_text())
        b_report = json.loads(Path(b['report']).read_text())
        checks = checks_against(a_report, b_report, a['csv'], b['csv'])
        paired.append({'pair': pair, 'checks': checks,
                       'releaseSpeedupRatio': a['scanMs'] / b['scanMs'],
                       'releaseElapsedReductionPercent': 100 * (1 - b['scanMs'] / a['scanMs'])})
    across = []
    if results:
        first = results[0]
        first_report = json.loads(Path(first['report']).read_text())
        for current in results[1:]:
            report = json.loads(Path(current['report']).read_text())
            across.append({'run': current['run'], 'checks': checks_against(
                first_report, report, first['csv'], current['csv'])})
    modes = {}
    for mode in ('debug', 'release'):
        runs = [r for r in results if r['mode'] == mode]
        if not runs:
            continue
        timings = [r['scanMs'] for r in runs]
        modes[mode] = {'scanMs': timings, 'medianScanMs': statistics.median(timings),
                       'minScanMs': min(timings), 'maxScanMs': max(timings),
                       'retainedMemory': [r['memoryRetained'] for r in runs]}
    summary = {'runs': results, 'pairs': paired, 'acrossAllRuns': across, 'modes': modes,
               'complete': len(results) == 6,
               'allSemanticChecksPass': all(all(v for k, v in p['checks'].items()
                    if k != 'propertyAllocationCapacities') for p in paired + across)}
    if len(modes) == 2:
        summary['medianReleaseSpeedupRatio'] = modes['debug']['medianScanMs'] / modes['release']['medianScanMs']
        summary['medianReleaseElapsedReductionPercent'] = 100 * (
            1 - modes['release']['medianScanMs'] / modes['debug']['medianScanMs'])
    (OUT / 'comparison-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if (OUT / 'run-results.json').exists():
        raise RuntimeError('Measurement results already exist; preserve them before a new run.')
    initial = stamps()
    method = {
        'host': {'platform': platform.platform(), 'machine': platform.machine(),
                 'macOS': command_output(['sw_vers']),
                 'hardware': command_output(['sysctl', '-n', 'hw.model', 'hw.ncpu', 'hw.memsize'])},
        'roots': ROOTS, 'rootSemicolonEnvironment': ';'.join(ROOTS),
        'order': [{'pair': pair, 'mode': mode} for pair, mode in ORDER],
        'method': 'Six sequential fresh scanner processes; pair order Debug/Release, Release/Debug, Debug/Release; explicitly native arm64.',
        'cache': 'Warm or uncontrolled OS filesystem cache: collection previously scanned, no cache purge or cold-cache claim.',
        'scanTimingDefinition': 'tst_scanner QElapsedTimer from scanner.startScan through scanFinished and scanIssuesFinished delivery; includes discovery, parsing, archive packing, reconciliation and formatted adapter rows; excludes CSV write and source restoration/count inspection.',
        'memoryDefinition': 'task_vm_info residentBytes and phys_footprint plus getrusage peak RSS captured after scan completion with session and adapter rows retained, before CSV or temporary source restoration. GUI model is not populated.',
        'externalTimeDefinition': '/usr/bin/time -l and processWallSeconds cover whole process including CSV, scoped one-source-at-a-time restoration and graph counts; not the scan elapsed figure.',
        'environmentOverrides': {'QT_HASH_SEED': '0', 'QT_QPA_PLATFORM': 'offscreen'},
        'sourceCountForStamps': len(SOURCE_PATHS), 'folderCountForStamps': len(FOLDERS),
        'inputs': initial,
        'binaries': {'debug': {'path': str(DEBUG), 'sha256': digest(DEBUG)},
                     'release': {'path': str(RELEASE), 'sha256': digest(RELEASE)}},
        'comparisonScope': 'Every CSV byte, inventory row, issue, source order/status/read reason, archive sizes/block counts and semantic graph counts. No new recursive all-field graph fingerprints; prior all-field proof remains the supporting preservation evidence for unchanged source/archive code.',
        'limitations': ['One macOS arm64 machine and 2,413-file collection.',
                        'No Windows/NEXIS, 300,000-file, cold-cache or MDVX timing claim.',
                        'No source graphs retained by the runner; the scanner audit restores one source at a time after the memory reading.',
                        'Debug is an existing universal binary forced arm64; Release is an arm64-only separate exact-commit snapshot.'],
    }
    (OUT / 'method.json').write_text(json.dumps(method, indent=2) + '\n')
    results = []
    for index, (pair, mode) in enumerate(ORDER, 1):
        name = f'{index:02d}-pair{pair}-{mode}'
        executable = DEBUG if mode == 'debug' else RELEASE
        csv_path, report_path = OUT / f'{name}.csv', OUT / f'{name}.json'
        stdout_path, stderr_path = OUT / f'{name}.stdout.txt', OUT / f'{name}.stderr-time.txt'
        environment = dict(os.environ, QT_HASH_SEED='0', QT_QPA_PLATFORM='offscreen',
                           MEDIAMUSTER_CANON_REAL_SCAN_ROOTS=';'.join(ROOTS),
                           MEDIAMUSTER_CANON_REAL_SCAN_CSV=str(csv_path),
                           MEDIAMUSTER_CANON_REAL_SCAN_REPORT=str(report_path))
        command = ['/usr/bin/time', '-l', '/usr/bin/arch', '-arm64', str(executable),
                   'optional_read_only_real_scan']
        pre = stamps()
        started_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
        start = time.perf_counter()
        print(json.dumps({'event': 'started', 'run': name, 'utc': started_utc}), flush=True)
        with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
            process = subprocess.run(command, cwd=ROOT, env=environment, stdout=stdout, stderr=stderr)
        duration = time.perf_counter() - start
        post = stamps()
        if process.returncode:
            raise RuntimeError(f'{name} failed with {process.returncode}; see {stdout_path} and {stderr_path}')
        if pre != post or pre != initial:
            raise RuntimeError(f'Inputs changed before/during {name}')
        report = json.loads(report_path.read_text())
        result = {'run': name, 'pair': pair, 'mode': mode, 'command': command,
                  'startedUtc': started_utc, 'processWallSeconds': duration,
                  'returncode': process.returncode, 'sourceAndFolderStampsStable': True,
                  'scanMs': report['scanMs'], 'memoryBefore': report['memoryBefore'],
                  'memoryRetained': report['memoryRetained'], 'rows': report['rows'],
                  'sourceCount': len(report['sources']), 'issueCount': report['issueCount'],
                  'csv': str(csv_path), 'csvSha256': digest(csv_path), 'report': str(report_path),
                  'stdout': str(stdout_path), 'stderrTime': str(stderr_path)}
        results.append(result)
        (OUT / 'run-results.json').write_text(json.dumps(results, indent=2) + '\n')
        summary = summarize(results)
        print(json.dumps({'event': 'completed', 'run': name, 'scanMs': report['scanMs'],
                          'memoryRetained': report['memoryRetained'], 'csvSha256': result['csvSha256'],
                          'rows': report['rows'], 'sources': len(report['sources']),
                          'allSemanticChecksPassSoFar': summary['allSemanticChecksPass']}), flush=True)
    print(json.dumps({'event': 'all-complete', 'summary': str(OUT / 'comparison-summary.json')}), flush=True)


if __name__ == '__main__':
    main()
