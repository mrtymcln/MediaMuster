import os,json,time,collections
rows=[json.loads(x) for x in open('/tmp/canon-mxf-inventory.jsonl')][824:]
results=[]
for row in rows:
 p=row['path'];f=open(p,'rb');size=os.fstat(f.fileno()).st_size;pos=0;nread=0;nitems=0;counts=collections.Counter();start=time.monotonic();errors=[];parts=[]
 while pos<size:
  f.seek(pos);h=f.read(17);nread+=len(h)
  if len(h)!=17:errors.append('short header '+str(pos));break
  key=h[:16];first=h[16];w=first&127 if first&128 else 0
  if first==128 or w>8:errors.append('bad BER '+str(pos));break
  lb=f.read(w) if w else bytes([first]);nread+=w
  if len(lb)!=(w or 1):errors.append('short BER '+str(pos));break
  n=int.from_bytes(lb,'big');end=pos+17+w+n
  if end>size:errors.append('short value '+str(pos));break
  if key.startswith(bytes.fromhex('060e2b34020501010d01020101')) and key[13] in [2,3,4]:kind='partition';parts.append(pos)
  elif key.startswith(bytes.fromhex('060e2b34010201010d010301')):kind='gc essence'
  elif key.startswith(bytes.fromhex('060e2b340253')):kind='localset'
  elif key[:7]==bytes.fromhex('060e2b34010101') and key[8:]==bytes.fromhex('0301021001000000'):kind='fill'
  else:kind=key.hex()
  counts[kind]+=1;nitems+=1;pos=end
 results.append({'path':p,'size':size,'packetCount':nitems,'bytesRead':nread,'seconds':round(time.monotonic()-start,6),'kinds':dict(counts),'partitions':parts,'errors':errors})
 f.close()
open('/tmp/canon-mxf-fullwalk.json','w').write(json.dumps(results,indent=2))
for r in results:print(json.dumps(r))
