from pathlib import Path
import json,collections,hashlib,gzip,shutil
p=Path('docs/reviews/2026-10-03-format-audit/evidence')
LARGE=Path((p/'large-evidence-location.txt').read_text().splitlines()[0])
INPUT=p/'parser-results.jsonl'
if not INPUT.exists():INPUT=LARGE/'parser-results.jsonl'
rows=[json.loads(x) for x in INPUT.read_text().splitlines()];by={r['path']:r for r in rows};stats={'pathsByExtension':dict(collections.Counter(Path(r['path']).suffix.lower() for r in rows)),'acceptedAllPaths':all(r.get('ok',r.get('metadata',{}).get('valid',False)) for r in rows),'mxfUniqueFileIdentities':len({r['metadata']['fileId'] for r in rows if Path(r['path']).suffix.lower()=='.mxf'}),'mxfCodecNames':dict(collections.Counter(r['metadata']['codec'] for r in rows if Path(r['path']).suffix.lower()=='.mxf'))};out=[];unique=set();coding=collections.Counter();geom=collections.Counter();fixed=[];zero=[];props=collections.Counter()
U={'coding':'060e2b34010101020401060100000000','bits':'060e2b3401010102040105030a000000','sw':'060e2b34010101010401050202000000','sh':'060e2b34010101010401050201000000','dw':'060e2b34010101010401050102000000','dh':'060e2b34010101010401050101000000'}
# raw canonical entries extracted directly from model to avoid guessing display IDs
import re
s=Path('/tmp/mxf_baseline_data_model.h').read_text()
for short,name in [('dw','DisplayWidth'),('dh','DisplayHeight'),('sampleH','SampledHeight')]:
 m=re.search(r'MXF_ITEM_DEFINITION\(GenericPictureEssenceDescriptor, '+name+r',\s*MXF_LABEL\(([^)]*)\)',s);v=bytes(int(x.strip(),16) for x in m[1].split(','));v=bytearray(v);v[7]=1;U[short]=bytes(v).hex()
for k,v in list(U.items()):
 v=bytearray.fromhex(v);v[7]=1;U[k]=bytes(v).hex()
for line in (p/'raw-mxf.jsonl').open():
 r=json.loads(line);unique.add(hashlib.sha256(json.dumps(r['sets'],sort_keys=True).encode()).hexdigest());desc=[]
 for o in r['sets']:
  f=o['fields'];props.update(f.keys())
  if o['type'] in (0x28,0x29,0x51,0x48,0x47,0x42,0x5c):
   d={'type':o['type']}
   for k,ul in U.items():
    if ul in f:d[k]=f[ul] if k=='coding' else int(f[ul],16)
   desc.append(d)
   if 'coding' in d:coding[d['coding']]+=1
   if d.get('bits')==254:fixed.append({'path':r['path'],'raw':d,'decoded':by[r['path']]['metadata']})
   if 'sh' in d:geom[(d.get('sh'),d.get('sampleH'),d.get('dh'))]+=1
 out.append({'path':r['path'],'partition':r.get('partition'),'setCount':len(r['sets']),'descriptors':desc})
stats.update({'rawDistinctMetadataSignatures':len(unique),'rawPropertyOccurrences':dict(props),'rawCodecULCounts':dict(coding),'rawGeometryCounts':{str(k):v for k,v in geom.items()},'sentinel254Paths':len(fixed),'sentinel254ULCounts':dict(collections.Counter(x['raw'].get('coding') for x in fixed))})
(p/'corpus-summary.json').write_text(json.dumps(stats,indent=2));(p/'descriptor-observations.jsonl').write_text('\n'.join(json.dumps(r,separators=(',',':')) for r in out)+'\n');(p/'sentinel-254.json').write_text(json.dumps(fixed,indent=2));print(json.dumps({k:v for k,v in stats.items() if k not in ('rawPropertyOccurrences','mxfCodecNames','rawCodecULCounts','rawGeometryCounts')},indent=2))
with (p/'raw-mxf.jsonl').open('rb') as inp,gzip.open(p/'raw-mxf.jsonl.gz','wb') as dest:shutil.copyfileobj(inp,dest)
(p/'raw-mxf.jsonl').unlink()
