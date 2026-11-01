import json,subprocess,time,hashlib,concurrent.futures
from pathlib import Path
base=Path('/tmp/mediamuster-mdb-jped-20261101')
out=base/'current/results';out.mkdir(exist_ok=True)
inputs=json.loads((base/'corpus/inputs.json').read_text())
exe=str(base/'current/build/current_probe')
def run(task):
 row,mode=task; target=out/(row['sha256']+'.'+mode+'.json');started=time.time()
 try:
  proc=subprocess.run([exe,mode,row['path']],capture_output=True,timeout=180)
  result={'path':row['path'],'sha256':row['sha256'],'mode':mode,'elapsed_s':time.time()-started,'exit':proc.returncode,'stderr':proc.stderr.decode(errors='replace')}
  if proc.returncode==0:
   value=json.loads(proc.stdout);target.write_bytes(proc.stdout);result.update(files=len(value.get('files',[])),masters=len(value.get('masters',[])),reading=value.get('reading',value.get('outcome')),parseNs=value.get('parseNs'),notes=len(value.get('notes',[])),fileReferencesComplete=value.get('fileReferencesComplete'))
  else:target.with_suffix('.failed.txt').write_bytes(proc.stdout)
 except Exception as e:result={'path':row['path'],'mode':mode,'error':str(e)}
 print(json.dumps(result),flush=True);return result
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
 results=list(pool.map(run,[(r,m) for r in inputs for m in ['bella','bella-fast','old-graph']]))
checks=[]
for row in inputs:
 p=Path(row['path']);checks.append({'path':str(p),'before_sha256':row['sha256'],'after_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'before_mtime_ns':row['mtime_ns'],'after_mtime_ns':p.stat().st_mtime_ns})
(out/'runs.json').write_text(json.dumps(results,indent=2));(out/'after-checks.json').write_text(json.dumps(checks,indent=2))
