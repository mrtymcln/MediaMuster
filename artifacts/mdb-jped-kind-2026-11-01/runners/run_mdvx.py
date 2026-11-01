import json,pathlib,subprocess,hashlib,time,gzip,shutil,traceback
base=pathlib.Path('/tmp/mediamuster-mdb-jped-20261101/binary');out=base/'runtime';out.mkdir(exist_ok=True)
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
results=[]
for x in json.load(open('/tmp/mediamuster-mdb-jped-20261101/corpus/inputs.json')):
 p=x['path'];sid=x['sha256'];r={'path':p,'manifest_sha256':sid,'sha256_before':sha(p)};st=time.monotonic();tmp=out/(sid+'.json');err=out/(sid+'.stderr')
 try:
  with tmp.open('wb') as fo,err.open('wb') as fe:
   z=subprocess.run([str(base/'mdvx_probe'),p],stdout=fo,stderr=fe,timeout=90)
  r['exit_code']=z.returncode
  if z.returncode==0:
   d=json.load(tmp.open());r.update({k:v for k,v in d.items()if k not in ('native_dictionary','objects')})
   mobs=[v for v in d.get('objects',[])if v.get('class')in ('MOBJ','CMOB','SMOB')]
   with gzip.open(out/(sid+'.mobs.json.gz'),'wt')as f:json.dump({'path':p,'mobs':mobs},f,separators=(',',':'))
 except subprocess.TimeoutExpired:r['timeout']=True
 except Exception as ex:r['error']=str(ex)
 r['seconds']=time.monotonic()-st;r['sha256_after']=sha(p);r['unchanged']=r['sha256_before']==r['sha256_after']
 if tmp.exists():
  with tmp.open('rb')as src,gzip.open(out/(sid+'.json.gz'),'wb',compresslevel=4)as dst:shutil.copyfileobj(src,dst)
  tmp.unlink()
 results.append(r);json.dump(results,(out/'index.json').open('w'),indent=2);print(p,r.get('native_open_result'),r.get('native_compositions_count'),r.get('exit_code'),r.get('error'),flush=True)
