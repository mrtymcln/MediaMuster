from pathlib import Path
import subprocess,json,time,hashlib,gzip,sys,collections,datetime
repo=Path('/Users/martymclean/Developer/MediaMuster'); base=repo/'docs/reviews/2026-11-01-bella-mdb-review/evidence';out=base/'oldscanner'; probe=Path(sys.argv[1] if len(sys.argv)>1 else '/tmp/bella-mdb-oldscanner-current-build/probe'); inv=json.loads((base/'inventory.json').read_text());rows=[]
for i,entry in enumerate(inv['files']):
 path=entry['path'];start=time.monotonic()
 try:
  p=subprocess.run([str(probe),path],capture_output=True,text=True,timeout=90)
  row={'path':path,'exitCode':p.returncode,'elapsedSeconds':round(time.monotonic()-start,6),'inventorySha256':entry.get('sha256')}
  if p.stdout:
   try:row['report']=json.loads(p.stdout)
   except Exception as e:row['stdoutParseError']=str(e);row['stdoutPrefix']=p.stdout[:1000]
  if p.stderr:row['stderr']=p.stderr
 except Exception as e:row={'path':path,'error':str(e)}
 rows.append(row)
 if (i+1)%25==0:print(f'Processed {i+1}/{len(inv["files"])}',flush=True)
with gzip.open(out/'results.json.gz','wt') as f:json.dump(rows,f,separators=(',',':'))
summary={'paths':len(rows),'exitCodes':dict(collections.Counter(str(r.get('exitCode','error')) for r in rows)),'outcomes':dict(collections.Counter(str(r.get('report',{}).get('outcome','none')) for r in rows)),'elapsedSecondsTotal':sum(r.get('elapsedSeconds',0) for r in rows),'errors':[r for r in rows if r.get('exitCode')!=0 or 'stdoutParseError' in r]}
(out/'run-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)
