"""Read-only audit. Raw KLV/Bento1 framing plus independent pyavb comparison.
Run from repository root; no essence payloads are read by the KLV walk.
"""
from pathlib import Path
import struct,json,collections,sys,hashlib
ROOT=Path(__file__).resolve().parent
LARGE=Path((ROOT/'large-evidence-location.txt').read_text().splitlines()[0])
INPUT=ROOT/'parser-results.jsonl'
if not INPUT.exists():INPUT=LARGE/'parser-results.jsonl'
rows=[json.loads(x) for x in INPUT.read_text().splitlines()]
def num(b):return int.from_bytes(b,'big')
def dotted(b):return '.'.join(b[i:i+8].hex() for i in range(0,len(b),8))
def mxf(path):
 out={'path':path,'sets':[]};primer={};raw=[]
 with open(path,'rb') as f:
  head=f.read(65552);p=head.find(bytes.fromhex('060e2b34020501010d0102010102'))
  if p<0:raise ValueError('header missing')
  f.seek(p);first=True
  while True:
   at=f.tell();key=f.read(16)
   if len(key)<16:break
   if not first and (key[:14] in (bytes.fromhex('060e2b34020501010d0102010103'),bytes.fromhex('060e2b34020501010d0102010104')) or key[:12]==bytes.fromhex('060e2b34010201010d010301')):break
   first=False;x=f.read(1)
   if not x:break
   n=x[0] if x[0]<128 else num(f.read(x[0]&127));body=f.tell()
   if key==bytes.fromhex('060e2b34020501010d01020101050100'):
    b=f.read(n);count,stride=struct.unpack_from('>II',b)
    for i in range(count):
     tag=struct.unpack_from('>H',b,8+i*stride)[0];ul=bytearray(b[10+i*stride:26+i*stride])
     if ul[:4]==bytes.fromhex('060e2b34'):ul[7]=1
     primer[tag]=bytes(ul).hex()
   elif key[:7]==bytes.fromhex('060e2b34025301') and key[8:13]==bytes.fromhex('0d01010101'):
    b=f.read(n);fields={};i=0
    while i<len(b):
     tag,l=struct.unpack_from('>HH',b,i);i+=4;fields[tag]=b[i:i+l];i+=l
    raw.append((key[14],fields,at))
   elif key[13:14]==b'\x02' and key[:12]==bytes.fromhex('060e2b34020501010d010201'):
    b=f.read(min(n,104));out['partition']={'headerBytes':num(b[32:40]),'op':b[64:80].hex(),'firstContainer':b[88:104].hex()}
   f.seek(body+n)
  out['scannedTo']=f.tell()
 # authoritative property identities from downloaded libMXF model
 for typ,fields,at in raw:
  entry={'type':typ,'offset':at,'fields':{primer.get(t,hex(t)):v.hex() for t,v in fields.items()}}
  out['sets'].append(entry)
 return out
raw=[];errors=[]
for r in rows:
 if Path(r['path']).suffix.lower()=='.mxf':
  try:raw.append(mxf(r['path']))
  except Exception as e:errors.append({'path':r['path'],'error':repr(e)})
(ROOT/'raw-mxf.jsonl').write_text('\n'.join(json.dumps(r,separators=(',',':')) for r in raw)+'\n')
# Bento1 tail + dictionary independently, without using production reader.
bento=[]
for r in rows:
 if Path(r['path']).suffix.lower() not in ('.mdb','.omf','.aif','.wav'):continue
 with open(r['path'],'rb') as f:
  f.seek(-24,2);label=f.read(24)
  if label[:8]!=bytes.fromhex('a4434da5486472d7'):continue
  flags,extra,major,minor,off,length=struct.unpack_from('<HHHHII',label,8)
  info={'path':r['path'],'container':[major,minor],'flags':flags,'extra':extra,'tocOffset':off,'tocLength':length}
  if major==1:
   f.seek(off);toc=f.read(length);vals={};names={}
   for i in range(0,length,24):
    obj,prop,typ,val,l,generation,fl=struct.unpack_from('<IIIIIHH',toc,i)
    if fl&1:b=toc[i+12:i+12+l]
    elif prop==24 or l<=256:
     f.seek(val);b=f.read(l)
    else:continue
    vals[obj,prop]=b
    if prop==24:names[b.rstrip(b'\0').decode('ascii','replace')]=obj
   info['dictionary']=names
   for name in ['OMFI:Version','OMFI:HEAD:Version','OMFI:ByteOrder','OMFI:HEAD:ByteOrder']:
    prop=names.get(name);info[name]=[{ 'object':o,'bytes':b.hex()} for (o,p),b in vals.items() if p==prop]
  bento.append(info)
(ROOT/'raw-bento.json').write_text(json.dumps(bento,indent=2))
# Pure Python reference, deliberately disable optional compiled readers.
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
comparisons=[]
for r in rows:
 if Path(r['path']).suffix.lower()!='.avb':continue
 try:
  ids=set();mobs=[];classes=collections.Counter();skipped=[]
  with avb.file.AVBFile(r['path'],use_ext=False) as f:
   for i in range(1,len(f.object_positions)):
    try:obj=f.read_object(i)
    except Exception as e:skipped.append({'object':i,'error':str(e)});continue
    classes[obj.class_id.decode('ascii')]+=1
    if obj.class_id not in (b'MSML',b'CMPO'):continue
    data=obj.property_data
    if obj.class_id==b'MSML' and data.get('mob_id') is not None:ids.add(dotted(bytes(data['mob_id'].bytes_le)))
    if obj.class_id==b'CMPO' and data.get('mob_id') is not None:
     mob={'id':dotted(bytes(data['mob_id'].bytes_le)),'name':data.get('name') or '', 'type':data.get('mob_type_id'),'usage':data.get('usage_code'),'bin':''}
     attrs=data.get('attributes')
     if attrs and attrs.get('_ORG_BIN'):
      ref=attrs.get('_ORG_BIN');mob['bin']=ref.property_data.get('name_utf8') or ref.property_data.get('name') or ''
     mobs.append(mob)
  expected={json.dumps(m,sort_keys=True) for m in mobs};actual={json.dumps(m,sort_keys=True) for m in r['mobs']}
  comparisons.append({'path':r['path'],'classes':dict(classes),'referenceSkippedObjects':skipped,'mobCount':len(mobs),'mobsMatch':expected==actual,'fileIdsMatch':ids==set(r['fileIds']),'missingMobs':list(expected-actual),'extraMobs':list(actual-expected)})
 except Exception as e:comparisons.append({'path':r['path'],'error':repr(e)})
(ROOT/'pyavb-comparison.json').write_text(json.dumps(comparisons,indent=2))
summary={'rawMxfCount':len(raw),'rawMxfErrors':errors,'bentoCount':len(bento),'pyavbCount':len(comparisons),'pyavbMismatch':[r for r in comparisons if not r.get('mobsMatch') or not r.get('fileIdsMatch')]}
(ROOT/'independent-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
