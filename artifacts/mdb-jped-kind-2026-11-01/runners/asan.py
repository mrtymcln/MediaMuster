import json,subprocess,os,random,struct,hashlib,concurrent.futures
from pathlib import Path
base=Path('/tmp/mediamuster-mdb-jped-20261101');inputs=json.loads((base/'corpus/inputs.json').read_text());out=base/'current/asan';out.mkdir(exist_ok=True)
seed=20261101;rng=random.Random(seed);original=Path(next(r['path'] for r in inputs if r['sha256'].startswith('7b30fd'))).read_bytes();toc,size=struct.unpack_from('<II',original,len(original)-8)
cases=[]
for length in [0,1,7,23,24,len(original)-1,toc,toc+24]:cases.append((f'truncate-{length}',original[:length]))
for index in range(128):
 data=bytearray(original)
 if index<64:
  off=toc+rng.randrange(size//24)*24+rng.choice([0,4,8,12,16,20,22]); count=rng.choice([1,2,4]);count=min(count,len(data)-off)
 else:off=rng.randrange(len(data));count=1
 for i in range(count):data[off+i]^=rng.randrange(1,256)
 cases.append((f'bitmutation-{index}',bytes(data)))
tasks=[(r['path'],r['sha256'],'original') for r in inputs]
for name,data in cases:
 p=out/(name+'.mdb');p.write_bytes(data);tasks.append((str(p),hashlib.sha256(data).hexdigest(),'fresh-mutation'))
env=dict(os.environ,MDB_AUDIT_COUNT='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def run(task):
 path,sha,kind=task;rows=[]
 for mode in ['bella','old']:
  try:
   r=subprocess.run([str(base/'current/build-asan/current_probe'),mode,path],capture_output=True,env=env,timeout=60)
   row={'path':path,'sha256':sha,'kind':kind,'mode':mode,'exit':r.returncode,'stderr':r.stderr.decode(errors='replace')}
   if r.returncode==0:row['output']=json.loads(r.stdout)
  except Exception as e:row={'path':path,'kind':kind,'mode':mode,'error':str(e)}
  rows.append(row)
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=[row for group in pool.map(run,tasks) for row in group]
result={'seed':seed,'fresh_cases':len(cases),'original_inputs':len(inputs),'runs':results,'failures':[r for r in results if r.get('exit',0)!=0 or r.get('error') or r.get('stderr')]}
(out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps({'runs':len(results),'failures':len(result['failures']),'cases':len(cases),'originals':len(inputs)}))
