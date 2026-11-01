from pathlib import Path
import json,gzip,collections,hashlib,subprocess,re
base=Path(__file__).resolve().parent.parent;out=base/'oldscanner';old=json.load(gzip.open(out/'results.json.gz','rt'));bella={r['path']:r['metadata'] for r in json.load(gzip.open(base/'bella-reports.json.gz','rt'))['rows'] if r['includePrecomputeDetails']};inv={r['path']:r for r in json.loads((base/'inventory.json').read_text())['files']};norm=lambda v:v.replace('.','').lower();counts=collections.Counter();differences=[];paths=[]
def group(path):
 if not inv[path]['footerHex'].startswith('a4434da5486472d7'):return 'nonBento'
 return 'authored' if path.startswith('/private/tmp/') or '/.codex/' in path else 'specimenOrSpecimenDerivative'
def vals(r,f):
 vs=[o['value'] for o in r['fields'].get(f,[]) if o['eligible'] and o['state']==1]
 return [json.loads(v) for v in sorted({json.dumps(x,sort_keys=True) for x in vs})]
def check(path,ident,field,a,b):
 eq=a==b;counts[(group(path),field,'equal' if eq else 'different')]+=1
 if not eq:differences.append({'path':path,'identity':ident,'field':field,'oldscanner':a,'bella':b})
for run in old:
 path=run['path'];o=run['report'];b=bella[path];c=b['catalogue'];of={norm(f['fileId']):f for f in o['files']};bf={f['fileMobId']['bytesHex']:f for f in c['files']};om={norm(i):m for m in o['masters'] for i in m['masterIds']};bm={m['masterMobId']['bytesHex']:m for m in c['masters']}
 row={'path':path,'group':group(path),'sha256':inv[path]['sha256'],'oldscannerOutcome':o['outcome'],'bellaReading':b['reading'],'oldFileCount':len(o['files']),'bellaFileCount':len(c['files']),'oldMasterCount':len(o['masters']),'bellaMasterCount':len(c['masters']),'fileIdSetsEqual':set(of)==set(bf),'masterIdSetsEqual':set(om)==set(bm),'oldFileReferencesComplete':o['fileReferencesComplete'],'bellaFileReferencesComplete':c['fileReferencesComplete'],'oldDiagnosticCount':len(o['diagnostics']),'oldProjectionDiagnosticCount':len(o['projectionDiagnostics'])};paths.append(row)
 for i in of.keys()&bf.keys():
  a,z=of[i],bf[i];v=vals(a,'Project');check(path,i,'Project',v if v else [''],[z['project']['value']])
  check(path,i,'directMasterIds',sorted(norm(j) for j in a['masterIds']),sorted(c['masters'][j]['masterMobId']['bytesHex'] for j in z['directMasterRecords']))
  if z['kind']!=0:check(path,i,'knownKind',vals(a,'Kind'),[1 if z['kind']==1 else 0])
  v=vals(a,'File Duration');units=sorted({x['Units'] for x in v if isinstance(x,dict) and 'Units'in x});
  if units and z['length'] is not None:check(path,i,'knownDurationUnits',units,[z['length']])
 for i in om.keys()&bm.keys():
  a,z=om[i],bm[i]
  for field,key in [('Clip Name','clipName'),('Original Bin','originalBin')]:
   v=vals(a,field);check(path,i,field,v if v else [''],[z[key]['value']])
  v=vals(a,'Type');check(path,i,'Type',v if v else [None],[z['type']-1 if z['type'] else None])
  v=vals(a,'Precompute Category');check(path,i,'Precompute Category',v if v else [0],[z['precomputeCategory']])
summary={'scope':'Direct MDB reader+projector only, no coordinator/scanner or media-header inputs; IDs normalized only by removing OldScanner display separators. Metadata comparisons use eligible Present observations, preserving distinct competing values. Counts include duplicate paths and authored fixtures.','allPaths':len(paths),'processesNormalExit':sum(r['exitCode']==0 for r in old),'fileCountsEqualPaths':sum(r['oldFileCount']==r['bellaFileCount'] for r in paths),'masterCountsEqualPaths':sum(r['oldMasterCount']==r['bellaMasterCount'] for r in paths),'fileIdSetsEqualPaths':sum(r['fileIdSetsEqual'] for r in paths),'masterIdSetsEqualPaths':sum(r['masterIdSetsEqual'] for r in paths),'outcomes':dict(collections.Counter(str(r['oldscannerOutcome']) for r in paths)),'outcomeVsBella': [{'oldscannerOutcome':a,'bellaReading':b,'paths':n} for (a,b),n in sorted(collections.Counter((r['oldscannerOutcome'],r['bellaReading']) for r in paths).items())],'countRows':[{'group':g,'field':f,'result':r,'count':n}for(g,f,r),n in sorted(counts.items())],'perPath':paths,'differences':differences}
(out/'comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
repo=Path('/Users/martymclean/Developer/MediaMuster');deps=set()
for p in Path('/tmp/bella-mdb-oldscanner-current-build/CMakeFiles/probe.dir').rglob('*.o.d'):
 deps.update(re.findall(str(repo)+r'/src/[^\s\\]+',p.read_text()))
manifest=[{'path':str(Path(p).relative_to(repo)),'sha256':hashlib.sha256(Path(p).read_bytes()).hexdigest()}for p in sorted(deps) if Path(p).is_file()]
receipt={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True,cwd=repo).strip(),'build':'Fresh isolated CMake native arm64 Release build from current worktree, Qt6.5.3 Core, all build steps exit0','configuration':['-DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos','-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_BUILD_TYPE=Release'],'sourceFiles':manifest,'probeSha256':hashlib.sha256((out/'probe.cpp').read_bytes()).hexdigest(),'binarySha256':hashlib.sha256(Path('/tmp/bella-mdb-oldscanner-current-build/probe').read_bytes()).hexdigest(),'resultsFile':'results.json.gz','resultsSha256':hashlib.sha256((out/'results.json.gz').read_bytes()).hexdigest(),'inputInventorySha256':hashlib.sha256((base/'inventory.json').read_bytes()).hexdigest(),'inputIntegrity':'Inputs opened read-only; inventory hashes reused as baseline identifiers. This runner does not claim a second independent full source rehash or atomic locking.'}
(out/'build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['perPath','differences']},indent=2));print('differenceExamples',json.dumps([r for r in differences if group(r['path'])=='specimenOrSpecimenDerivative'][:8],indent=2))
