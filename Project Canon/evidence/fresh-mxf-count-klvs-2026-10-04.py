from pathlib import Path
import json, struct, collections, time
roots=[Path('/Volumes/EDIT/Avid MediaFiles/MXF'),Path('/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF'),Path('/Users/martymclean/Desktop/Avid MediaFiles/MXF')]
def readone(p):
 s=p.stat().st_size; packets=collections.Counter(); nbytes=0; parts=[]; unknown=[]; n=0; runin=0
 with p.open('rb',buffering=0) as f:
  k=f.read(16); nbytes+=len(k)
  if k[:11]!=bytes.fromhex('060e2b34020501010d0102'):
   f.seek(0); lead=f.read(65551); nbytes+=len(lead); i=lead.find(bytes.fromhex('060e2b34020501010d0102'))
   if not 0<=i<=65535:return {'path':str(p),'status':'noheader','size':s}
   runin=i; f.seek(i); k=f.read(16); nbytes+=16
  pos=runin
  while pos<s:
   if len(k)!=16:raise ValueError(('key',pos,k.hex()))
   b=f.read(1); nbytes+=len(b)
   if not b:raise ValueError(('len',pos))
   if b[0]<128:l=b[0]; ll=1
   else:
    count=b[0]&127
    if not 1<=count<=8:raise ValueError(('ber',pos,b.hex()))
    v=f.read(count); nbytes+=len(v)
    if len(v)!=count:raise ValueError(('ber_short',pos))
    l=int.from_bytes(v,'big');ll=1+count
   end=pos+16+ll+l
   if end>s:raise ValueError(('bounds',pos,l,s))
   n+=1; typ='other'; key=k.hex()
   if k[:13]==bytes.fromhex('060e2b34020501010d01020101') and k[13] in (2,3,4):
    typ='partition'; val=f.read(min(l,88)); nbytes+=len(val); parts.append({'pos':pos,'length':l,'kind':k[13],'status':k[14],'headerbytes':int.from_bytes(val[32:40],'big')})
   elif k[0:5]==bytes.fromhex('060e2b3402') and k[5] in (0x53,0x13):typ='localset'
   elif k[:7]+k[8:]==bytes.fromhex('060e2b340101010301021001000000'):typ='fill'
   elif k[:13]==bytes.fromhex('060e2b34020501010d01020101') and k[13]==5:typ='primer'
   elif k[:13]==bytes.fromhex('060e2b34020501010d01020101') and k[13]==0x11:typ='rip'
   elif k[:5]==bytes.fromhex('060e2b3401') and k[8:12] in (bytes.fromhex('0d010301'),bytes.fromhex('0e040301')):typ='essence_or_system'
   else:
    if len(unknown)<30:unknown.append({'key':key,'pos':pos,'length':l})
   packets[typ]+=1;pos=end;f.seek(pos);k=f.read(16) if pos<s else b'';nbytes+=len(k)
 return {'path':str(p),'size':s,'status':'complete','klvs':n,'read':nbytes,'packets':dict(packets),'partitions':parts,'unknown':unknown,'runin':runin}
files=[]
for root in roots:
 if not root.exists():continue
 for child in root.iterdir():
  if child.is_dir() and not child.is_symlink():files.extend(p for p in child.iterdir() if p.is_file() and p.suffix.lower()=='.mxf')
t=time.monotonic();out=[]
for p in files:
 try:out.append(readone(p))
 except Exception as e:out.append({'path':str(p),'status':'error','error':repr(e)})
Path('/private/tmp/mediamuster-mxf-reference/klv-counts.jsonl').write_text('\n'.join(json.dumps(x) for x in out)+'\n')
complete=[x for x in out if x['status']=='complete'];print(json.dumps({'seconds':time.monotonic()-t,'files':len(out),'status':dict(collections.Counter(x['status'] for x in out)),'totalbytes':sum(x['size'] for x in complete),'readbytes':sum(x['read'] for x in complete),'klv_min':min(x['klvs'] for x in complete),'klv_max':max(x['klvs'] for x in complete),'top':sorted([(x['klvs'],x['path']) for x in complete],reverse=True)[:10],'packets':dict(sum((collections.Counter(x['packets']) for x in complete),collections.Counter()))},indent=2))
