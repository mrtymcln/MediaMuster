import json,collections,re
from pathlib import Path
base=Path('/tmp/mediamuster-mdb-jped-20261101');out=base/'current/results'
def normid(i):return i.replace('.','').lower()
def val(o,p):return o['fields'][p]['value']
def oldrate(x):return (x.get('Numerator'),x.get('Denominator')) if isinstance(x,dict) else None
def brate(x):return (x['n'],x['d']) if isinstance(x,dict) else None
def rateeq(a,b):return a==b or (a and b and a[0]*b[1]==a[1]*b[0])
diffs=[];counts=collections.Counter();summaries=[];extra=collections.Counter();descriptors=collections.Counter()
for row in json.loads((base/'corpus/inputs.json').read_text()):
 if not row['avid_bento']:continue
 key=row['sha256'];b=json.loads((out/(key+'.bella.json')).read_text());o=json.loads((out/(key+'.old-graph.json')).read_text());fast=json.loads((out/(key+'.bella-fast.json')).read_text())
 oldfiles={normid(f['id']):f for f in o['files']};oldmasters={normid(m['masterIds'][0]):m for m in o['masters']};bm={m['id']:m for m in b['masters']}
 info={'path':row['path'],'sha256':key,'bella_files':len(b['files']),'bella_masters':len(b['masters']),'old_files':len(o['files']),'old_masters':len(o['masters']),'only_bella_files':sorted(set(f['id'] for f in b['files'])-set(oldfiles)),'only_old_files':sorted(set(oldfiles)-set(f['id'] for f in b['files'])),'only_bella_masters':sorted(set(bm)-set(oldmasters)),'only_old_masters':sorted(set(oldmasters)-set(bm))}
 summaries.append(info)
 def check(identity,field,bv,ov,eq=None):
  counts[field]+=1
  if (eq(bv,ov) if eq else bv==ov):return
  diffs.append({'path':row['path'],'sha256':key,'id':identity,'field':field,'bella':bv,'old':ov})
 for f in b['files']:
  descriptors[f['descriptorClass']]+=1
  g=oldfiles.get(f['id'])
  if not g:continue
  check(f['id'],'project',f['project'],val(g,'Project') or '')
  check(f['id'],'kind',f['kind'],{None:0,0:2,1:1}.get(val(g,'Kind')))
  bittext=val(g,'Bit Depth') or ''
  bitmatch=re.match(r'^(\d+)-bit',bittext)
  check(f['id'],'bits',f['bitDepth']['bits'],int(bitmatch[1]) if bitmatch else None)
  if f['bitDepth']['rawComponentWidth'] is not None:check(f['id'],'rawComponentWidth',f['bitDepth']['rawComponentWidth'],val(g,'Component Depth'))
  duration=val(g,'File Duration') or {}
  check(f['id'],'length',f['length'],duration.get('Units'))
  check(f['id'],'sampleRate',brate(f['sampleRate']),oldrate(duration.get('Rate')),rateeq)
  check(f['id'],'audioEditRate',brate(f['audioEditRate']),oldrate(duration.get('DisplayRate')) if f['kind']==1 else None,rateeq)
  if f.get('resolution'):res=f"{f['resolution']['w']}x{f['resolution']['h']}"
  else:res=None
  check(f['id'],'resolution',res,val(g,'Resolution'))
  recordedlabels=[ob['value'].removeprefix('hex:') for ob in g['observations'].get('Compression Label',[]) if ob['property']=='OMFI:DIDD:EssenceCompression']
  check(f['id'],'recordedCodingLabel',f['compression']['codingLabel'] if f['compression']['codingState']==2 else None,recordedlabels[0] if recordedlabels else None)
  if f['numbering'] in [3,4]:check(f['id'],'recordedDropFrame',f['numbering']==4,val(g,'Drop Frame'))
  if f.get('audioFrequency') is not None:
   hz=f['audioFrequency']
   if isinstance(hz,str):
    raw=bytes.fromhex(hz);exponent=int.from_bytes(raw[:2],'big');mantissa=int.from_bytes(raw[2:],'big');hz=(-1 if exponent&0x8000 else 1)*mantissa*2.0**((exponent&0x7fff)-16383-63)
    hz=(hz,1)
   elif isinstance(hz,dict):hz=(hz['n'],hz['d'])
   else:hz=(hz,1)
   check(f['id'],'audioFrequency',hz,oldrate(val(g,'Sample Rate')),rateeq)
  check(f['id'],'masters',sorted(b['masters'][i]['id'] for i in f['masterIndices']),sorted(normid(i) for i in g['masterIds']))
  for p in ['Source Path','Source Container','Imported','Channels','Created']:
   if p in g['fields'] and val(g,p) is not None:extra[p]+=1
 for m in b['masters']:
  g=oldmasters.get(m['id'])
  if not g:continue
  check(m['id'],'clipName',m['clipName'],val(g,'Clip Name') or '')
  check(m['id'],'originalBin',m['originalBin'],val(g,'Original Bin') or '')
  check(m['id'],'type',m['type'],{None:0,0:1,1:2}.get(val(g,'Type')))
  check(m['id'],'precomputeCategory',m['precomputeCategory'],val(g,'Precompute Category') or 0)
 for f,g in zip(b['files'],fast['files']):check(f['id'],'detailModeFiles',f,g)
 for m,g in zip(b['masters'],fast['masters']):
  check(m['id'],'detailModeMastersExceptCategory',{k:v for k,v in m.items() if k!='precomputeCategory'},{k:v for k,v in g.items() if k!='precomputeCategory'})
result={'inputs':summaries,'comparisons':dict(counts),'differences_by_field':dict(collections.Counter(d['field'] for d in diffs)),'old_additional_field_counts':dict(extra),'descriptors':dict(descriptors),'differences':diffs}
(base/'current/comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ['differences']},indent=2));print('first 20 discrepancies',json.dumps([d for d in diffs if d['field']!='precomputeCategory'][:20],indent=2))
