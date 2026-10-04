import json, collections
from pathlib import Path
rows=[json.loads(s) for s in Path('/tmp/canon-mxf-diagnostic-expanded-final.jsonl').read_text().splitlines() if s.strip()]
unknown=collections.Counter(); sets=collections.Counter()
for row in rows:
    unknown.update(row['unknownProperties'])
    sets.update(row['unknownSets'])
summary={
    'files':len(rows),
    'outcomes':dict(collections.Counter(x['outcome'] for x in rows)),
    'objects':sum(x['objects'] for x in rows),
    'properties':sum(x['properties'] for x in rows),
    'decoded':sum(x['decoded'] for x in rows),
    'knownTypes':sum(x['knownTypes'] for x in rows),
    'unreadable':sum(len(x['unreadable']) for x in rows),
    'missingMappings':sum(x['missingMappings'] for x in rows),
    'unknownSetOccurrences':sum(sets.values()),
    'unknownSets':dict(sets),
    'unknownPropertyOccurrences':sum(unknown.values()),
    'unknownProperties':dict(unknown),
    'diagnostics':dict(collections.Counter(d for x in rows for d in x['diagnostics']))
}
Path('/tmp/canon-mxf-diagnostic-summary-final.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ('unknownSets','unknownProperties','diagnostics')},indent=2))
