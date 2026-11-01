import json,hashlib
from pathlib import Path
before=Path('/tmp/mediamuster-mdb-fresh-20261101');after=Path('/tmp/mediamuster-mdb-jped-20261101');manifest=json.loads((after/'corpus/inputs.json').read_text())
def differences(a,b,path=''):
 if type(a)!=type(b):return [{'field':path,'before':a,'after':b}]
 if isinstance(a,dict):
  if a.keys()!=b.keys():return [{'field':path,'before_keys':sorted(a),'after_keys':sorted(b)}]
  return [d for k in a if k!='parseNs' for d in differences(a[k],b[k],path+'.'+k)]
 if isinstance(a,list):
  if len(a)!=len(b):return [{'field':path,'before_length':len(a),'after_length':len(b)}]
  return [d for i,(x,y) in enumerate(zip(a,b)) for d in differences(x,y,path+f'[{i}]')]
 return [] if a==b else [{'field':path,'before':a,'after':b}]
rows=[];errors=[]
for r in manifest:
 for mode in ['bella','bella-fast']:
  name=r['sha256']+'.'+mode+'.json';a=json.loads((before/'current/results'/name).read_text());b=json.loads((after/'current/results'/name).read_text());ds=differences(a,b)
  expected=[]
  for i,f in enumerate(a['files']):
   if f['descriptorClass']=='JPED':
    if not f['descriptorQualified'] or f['kind']!=0:errors.append({'path':r['path'],'mode':mode,'unexpected_baseline':f})
    expected.append({'field':f'.files[{i}].kind','before':0,'after':2})
  if ds!=expected:errors.append({'path':r['path'],'mode':mode,'expected':expected,'actual':ds})
  rows.append({'path':r['path'],'sha256':r['sha256'],'mode':mode,'files':len(b['files']),'masters':len(b['masters']),'reading':b['reading'],'notes':b['notes'],'expected_jped_kind_changes':len(expected),'observed_changes':ds,'passed':ds==expected})
checks=[]
for r in manifest:
 p=Path(r['path']);actual=hashlib.sha256(p.read_bytes()).hexdigest();checks.append({'path':str(p),'expected':r['sha256'],'actual':actual,'unchanged':actual==r['sha256']})
 if actual!=r['sha256']:errors.append({'path':str(p),'input_changed':True})
current=json.loads((after/'current/comparison.json').read_text())
if current['differences_by_field']!={'project':86}:errors.append({'unexpected_oldscanner_disagreements':current['differences_by_field']})
result={'before_source_commit':'20220f2b4c2b7ff6ef3d017a77568e5edf7de7cd','expected_only_semantic_change':'Qualified JPED files change Kind Unknown (0) to Video (2).','report_comparisons':len(rows),'changed_jped_files_per_mode':{m:sum(r['expected_jped_kind_changes'] for r in rows if r['mode']==m) for m in ['bella','bella-fast']},'rows':rows,'input_checks':checks,'remaining_oldscanner_disagreements':current['differences_by_field'],'errors':errors,'passed':not errors}
(after/'delta-verification.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k not in ['rows','input_checks']},indent=2))
assert not errors
