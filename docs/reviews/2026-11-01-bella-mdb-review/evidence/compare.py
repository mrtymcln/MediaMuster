#!/usr/bin/env python3
"""Compare overlapping MDB facts, preserving intentional source distinctions."""
from collections import Counter, defaultdict
import gzip
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent
reports = json.loads(gzip.decompress((OUT / 'bella-reports.json.gz').read_bytes()))['rows']
bella = {r['path']: r for r in reports if r['includePrecomputeDetails']}
native = json.loads(gzip.decompress((OUT / 'mdvx-results.json.gz').read_bytes()))['rows']
inventory = json.loads((OUT / 'inventory.json').read_text())['files']
by_path = {f['path']: f for f in inventory}
counts = Counter()
differences = defaultdict(list)
open_differences = []
keys = []

def group(path):
    if not by_path[path]['footerHex'].startswith('a4434da5486472d7'):
        return 'nonBento'
    return 'authored' if path.startswith('/private/tmp/') or '/.codex/' in path else 'specimenOrSpecimenDerivative'

def text_conversion(a, b):
    try:
        return a.encode('mac_roman').decode('latin1') == b
    except UnicodeEncodeError:
        return False

for run in native:
    path = run['path']
    report = bella[path]['metadata']
    cat = report['catalogue']
    if (report['reading'] == 1) != run['native']['opened']:
        open_differences.append({'path':path, 'bellaReading':report['reading'], 'mdvxOpened':run['native']['opened']})
    for kind, records, id_name in [('file',cat['files'],'fileMobId'),('master',cat['masters'],'masterMobId')]:
        seen = defaultdict(set)
        for record in records:
            value = record[id_name]['bytesHex']
            if len(value) == 64:
                seen[value[32:48]].add(value)
        for key, values in seen.items():
            if len(values) > 1:
                keys.append({'path':path,'kind':kind,'eightByteKey':key,'distinctFullIds':sorted(values)})
    for row in run['native'].get('rows',[]):
        query = row['query']
        fi, mi = query['fileIndex'], query['masterIndex']
        file = cat['files'][fi] if fi is not None else None
        master = cat['masters'][mi] if mi is not None else None
        values = {}
        if master:
            values['clipName'] = (master['clipName']['value'], row['nameInBin'] or '')
            values['typeUsage'] = (7 if master['type'] == 1 else 1 if master['type'] == 2 else 0, row['usageCode'])
        if file:
            values['project'] = (file['project']['value'], row['project'] or '')
            if file['bitDepth']['bits'] is not None:
                values['bits'] = (file['bitDepth']['bits'], row['bits'])
            if file['compression']['resolutionId'] is not None:
                values['resolutionId'] = (file['compression']['resolutionId'], row['resolutionId'])
            if file['compression']['legacyCompression']:
                values['legacyCompression'] = (file['compression']['legacyCompression'], row['compressionFourCCHex'])
            audio = file['audioFrequency']
            if audio and audio['alternative'] == 0:
                values['pcmaFrequency'] = ([audio['value']['numerator'], audio['value']['denominator']], row['sampleRate'])
        for field, (a, b) in values.items():
            equal = a == b
            counts[(group(path),field,'equal' if equal else 'different')] += 1
            if equal:
                continue
            reason = 'Unclassified authored-input difference; no native parity assertion'
            if group(path) != 'authored':
                if field in ('clipName','project') and text_conversion(a,b):
                    reason = 'Bella MacRoman vs MDVX ISO-8859-1'
                elif field == 'project' and file['projectSelection'] == 3 and not b:
                    reason = 'Bella immediate-source Project recovery; MDVX own-file lookup leaves blank'
                elif field == 'typeUsage' and b == 15:
                    reason = 'MDVX import-setting usage override15; Bella retains own numeric master Type'
                elif field == 'bits' and b == 254 and file['bitDepth']['rawComponentWidth'] == 254:
                    reason = 'MDVX raw ComponentWidth254; Bella qualified numeric precision'
                elif field == 'bits' and b == 0 and file['descriptorClass'] in ('57415644','41494644'):
                    reason = 'Bella audio Summary depth; MDVX helper leaves depth zero'
                elif field == 'bits' and b == 0 and file['descriptorClass'] == '52474241':
                    reason = 'Bella RGBA component-array depth; MDVX helper leaves depth zero'
                else:
                    reason = 'REQUIRES INVESTIGATION'
            differences[(group(path),field)].append({'path':path,'fileIndex':fi,'masterIndex':mi,
                                                     'bella':a,'mdvx':b,'reason':reason})

detail_changes = []
off = {r['path']:r for r in reports if not r['includePrecomputeDetails']}
for path, on in bella.items():
    left, right = json.loads(json.dumps(on)), json.loads(json.dumps(off[path]))
    left.pop('includePrecomputeDetails'); right.pop('includePrecomputeDetails')
    for a, b in zip(left['metadata']['catalogue']['masters'], right['metadata']['catalogue']['masters']):
        a['precomputeCategory'] = b['precomputeCategory']
    if left != right:
        detail_changes.append(path)

result = {'scope':'Counts are per Bella-selected file/master query, including duplicate specimen paths; not independent physical-file or membership counts.',
          'countRows':[{'group':g,'field':f,'result':r,'count':c} for (g,f,r),c in sorted(counts.items())],
          'openDifferences':open_differences,
          'differences':[{'group':g,'field':f,'rows':r} for (g,f),r in differences.items()],
          'eightByteKeyCollisionsAmongDistinctBellaIds':keys,
          'detailToggleDifferencesOutsideCategory':detail_changes,
          'perPath':[{'path':r['path'],'group':group(r['path']),
                      'bellaReading':bella[r['path']]['metadata']['reading'],
                      'mdvxOpened':r['native']['opened'],
                      'mdvxCompositionCount':r['native']['compositionCount'],
                      'bellaFileRecords':len(bella[r['path']]['metadata']['catalogue']['files']),
                      'bellaMasterRecords':len(bella[r['path']]['metadata']['catalogue']['masters']),
                      'nativeQueries':len(r['native'].get('rows',[]))} for r in native]}
(OUT / 'comparison.json').write_text(json.dumps(result,indent=2) + '\n')
print(json.dumps({'counts':result['countRows'],
                  'reasonCounts':dict(Counter(r['reason'] for d in result['differences'] if d['group'] != 'authored' for r in d['rows'])),
                  'unexpectedToggleChanges':detail_changes, 'eightByteKeyCollisionGroups':len(keys)},indent=2))
