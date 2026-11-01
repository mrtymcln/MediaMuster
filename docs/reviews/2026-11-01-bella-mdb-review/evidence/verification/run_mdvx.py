#!/usr/bin/env python3
"""Run each inventoried path in an isolated MDVX process and retain outcomes."""
import concurrent.futures
import hashlib
import gzip
import json
import os
from pathlib import Path
import subprocess
import time

OUT = Path(__file__).resolve().parent
WORK = Path('/private/tmp/bella-mdb-verification-20261101/mdvx')
WORK.mkdir(parents=True, exist_ok=True)
reports = {r['path']: r for r in json.loads(gzip.decompress((OUT / 'bella-reports.json.gz').read_bytes()))['rows']
           if r['includePrecomputeDetails']}
inventory = json.loads((OUT / 'inventory.json').read_text())

def run(entry):
    path = entry['path']
    token = hashlib.sha256(path.encode()).hexdigest()[:20]
    catalogue = reports[path]['metadata']['catalogue']
    queries = []
    used = set()
    for fi, file in enumerate(catalogue['files']):
        for mi in file['directMasterRecords'] or [None]:
            master = catalogue['masters'][mi] if mi is not None else None
            if mi is not None:
                used.add(mi)
            queries.append({'fileIndex': fi, 'masterIndex': mi,
                            'fileHex': file['fileMobId']['bytesHex'],
                            'masterHex': master['masterMobId']['bytesHex'] if master else ''})
    for mi, master in enumerate(catalogue['masters']):
        if mi not in used:
            queries.append({'fileIndex': None, 'masterIndex': mi, 'fileHex': '',
                            'masterHex': master['masterMobId']['bytesHex']})
    qp = WORK / (token + '-queries.json')
    op = WORK / (token + '-result.json')
    qp.write_text(json.dumps(queries))
    env = dict(os.environ, MDB_REVIEW_INPUT=path, MDB_REVIEW_QUERIES=str(qp),
               MDB_REVIEW_OUTPUT=str(op), DYLD_INSERT_LIBRARIES=str(WORK / 'harness.dylib'))
    start = time.monotonic()
    result = {'path': path, 'inventorySha256': entry['sha256'], 'queries': len(queries)}
    before = os.stat(path)
    try:
        p = subprocess.run([str(WORK / 'MDVx')], env=env, capture_output=True, timeout=12)
        result.update(exitCode=p.returncode, stdout=p.stdout.decode(errors='replace'),
                      stderr=p.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as exc:
        result.update(timeout=True, stdout=(exc.stdout or b'').decode(errors='replace'),
                      stderr=(exc.stderr or b'').decode(errors='replace'))
    if op.exists():
        result['native'] = json.loads(op.read_text())
    after = os.stat(path)
    result['sourceStampUnchanged'] = (before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns)
    result['elapsedSeconds'] = time.monotonic() - start
    return result

results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
    futures = [executor.submit(run, entry) for entry in inventory['files']]
    for future in concurrent.futures.as_completed(futures):
        result = future.result()
        results.append(result)
        if len(results) % 25 == 0:
            print(f'{len(results)}/{len(futures)} paths attempted', flush=True)

receipt = {'method': 'Original arm64 MDB machine code in temporary ad-hoc-signed executable; constructor calls MDVxOMFI init/openFile/addInfoToItem before application main.',
           'limits': ['Query IDs and links come from Bella, so membership/association are not independently validated by this harness.',
                      'No GUI scan, PMR enrichment context, user settings or display/export code is run.',
                      'Original executable is unmodified; only the temporary copy is signed to permit library injection.'],
           'rows': sorted(results, key=lambda x: x['path'])}
for key, path in [('installedBinary',Path('/Applications/MDVx.app/Contents/MacOS/MDVx')),
                  ('runtimeBinary', WORK / 'MDVx'), ('harness', WORK / 'harness.dylib')]:
    receipt[key] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT / 'mdvx-results.json.gz').write_bytes(gzip.compress((json.dumps(receipt, indent=2) + '\n').encode(), mtime=0))
print(json.dumps({'attempted': len(results), 'successExit': sum(r.get('exitCode') == 0 for r in results),
                  'nativeOpenTrue': sum(r.get('native', {}).get('opened',False) for r in results),
                  'timeouts': sum(r.get('timeout',False) for r in results),
                  'crashes': sum(r.get('exitCode',0) < 0 for r in results)}), flush=True)
