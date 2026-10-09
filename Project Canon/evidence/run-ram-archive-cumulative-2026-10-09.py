import json,os,subprocess,hashlib
from pathlib import Path
path='/Volumes/EDIT/Avid MediaFiles/MXF/86452/msmMMOB.mdb'
def stamp():
 s=os.stat(path);return {'size':s.st_size,'mtimeNs':str(s.st_mtime_ns),'device':str(s.st_dev),'inode':str(s.st_ino)}
initial=stamp();results=[]
for mode in ['baseline','packed']:
 p=subprocess.run(['/private/tmp/canon-chunk-archive-series',mode,path],capture_output=True,text=True,env=dict(os.environ,QT_HASH_SEED='0'))
 if p.returncode:raise RuntimeError((mode,p.returncode,p.stderr))
 value=json.loads(p.stdout);value['stderr']=p.stderr;results.append(value);print(p.stdout,flush=True)
if stamp()!=initial:raise RuntimeError('Source changed')
for field in ['graphSha256','projectionSha256']:
 if results[0][field]!=results[1][field]:raise RuntimeError(field)
receipt={'method':'Two fresh sequential diagnostic processes, each retains three complete reads of the same unchanged genuine MDB. Baseline keeps three expanded graphs and their hot projections; packed mode retains three lossless RAM archives and the same hot projections. A controlled cumulative-storage experiment, not three distinct databases or physical-file rows and not a scanner benchmark.','sourceStamp':initial,'results':results,'sourceSha256':hashlib.sha256(Path('/private/tmp/canon-chunk-archive-series.cpp').read_bytes()).hexdigest(),'binarySha256':hashlib.sha256(Path('/private/tmp/canon-chunk-archive-series').read_bytes()).hexdigest(),'limitations':['One process per mode, baseline then packed. No repeated timing claim.','Archive verified as exact serialization bytes, not a typed struct/receipt-sharing roundtrip.','Current Debug Canon library/O2 diagnostic, Qt6.5.3 macOS arm64; no Windows/NEXIS or300k test.','The largest individual graph is still expanded during parse/projection.']}
Path('/private/tmp/canon-chunk-archive-series-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
