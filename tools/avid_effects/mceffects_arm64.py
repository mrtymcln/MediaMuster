"""Decode the hash-pinned MCEffects static registration initializer without loading it."""
import re,struct,json,collections,os
from pathlib import Path
B=Path(os.environ['AVID_EFFECT_PLUGIN']).read_bytes();segs=[];pos=32
for i in range(struct.unpack_from('<I',B,16)[0]):
 cmd,n=struct.unpack_from('<II',B,pos)
 if cmd==0x19:
  vm,sz,off,fsize=struct.unpack_from('<QQQQ',B,pos+24);segs.append((vm,sz,off,fsize))
 pos+=n
def raw(addr,n):
 for vm,sz,off,fs in segs:
  if vm<=addr and addr+n<=vm+fs:return B[off+addr-vm:off+addr-vm+n]
  if vm+fs<=addr and addr+n<=vm+sz:return bytes(n)
 raise ValueError(hex(addr))
def string(addr,width):
 addr &= (1<<36)-1
 b=bytearray()
 for i in range(512):
  c=raw(addr+i*width,width)
  if c==bytes(width):return b.decode('utf8' if width==1 else 'utf-32le')
  b+=c
 raise ValueError('string too long')
I=[]
for l in Path(os.environ['AVID_EFFECT_PLUGIN_ASM']).read_text().splitlines():
 m=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){3}[0-9a-f]{2}\s+([^\s]+)\s*(.*)',l)
 if m:I.append((int(m[1],16),m[2],m[3].split(';')[0].strip(),m[3]))
def run(alpha,first=0):
 r={'sp':0x9000000};mem={};cmp=(0,0)
 def get(k):
  if k.startswith('#'):return int(k[1:],0)
  if k in ('wzr','xzr'):return 0
  v=r.get('x'+k[1:] if k.startswith('w') else k,0)
  return v&0xffffffff if k.startswith('w') else v
 def put(k,v):r['x'+k[1:] if k.startswith('w') else k]=v& (0xffffffff if k.startswith('w') else (1<<128)-1)
 def addr(args):
  a=re.search(r'\[([^]]+)\]',args)[1].split(',');return get(a[0].strip())+(get(a[1].strip()) if len(a)>1 else 0)
 for pc,op,args,detail in I:
  a=[v.strip() for v in args.split(',')]
  if op=='adrp':put(a[0],(pc&~4095)+int(a[1])*4096)
  elif op=='mov':put(a[0],get(a[1]))
  elif op=='movk':put(a[0],get(a[0])|(get(a[1])<<int(a[2].split('#')[1])))
  elif op=='movi.2d':put('q'+a[0][1:],0)
  elif op in ['add','sub']:put(a[0],get(a[1])+(1 if op=='add' else -1)*get(a[2]))
  elif op=='cmp':cmp=(get(a[0]),get(a[1]))
  elif op=='csel':
   assert a[3] in ('eq','ne');put(a[0],get(a[1]) if (cmp[0]==cmp[1])==(a[3]=='eq') else get(a[2]))
  elif op in ['ldr','ldrb','ldrh']:
   ad=addr(args);n=1 if op=='ldrb' else 2 if op=='ldrh' else 16 if a[0].startswith('q') else 4 if a[0].startswith('w') else 8
   v=first if ad==0x8000000 else int.from_bytes(bytes(mem.get(ad+i,raw(ad+i,1)[0]) for i in range(n)),'little');put(a[0],v)
  elif op in ('stp','str','strh','strb','stur'):
   ad=addr(args);names=a[:2] if op=='stp' else a[:1]
   for k in names:
    n=1 if op=='strb' else 2 if op=='strh' else 16 if k.startswith('q') else 4 if k.startswith('w') else 8
    b=get(k).to_bytes(n,'little');mem.update({ad+i:v for i,v in enumerate(b)});ad+=n
  elif op=='bl':
   if 'GetMCFirstRef' in detail:put('x0',0x8000000)
   elif 'GetDynamicToggle' in detail:put('x0',alpha)
   else:raise ValueError(detail)
  elif op in ('ldp','ret'):pass
  else:raise ValueError((hex(pc),op,args))
 def read(ad,n):return int.from_bytes(bytes(mem[ad+i] for i in range(n)),'little')
 out=[]
 for i in range(162):
  ad=0x14aad0+i*0x48
  out.append(dict(index=i,id=read(ad,4),identifier=string(read(ad+0x10,8),1),name=string(read(ad+0x18,8),4),category=string(read(ad+0x20,8),4),alphaFlex=alpha))
 return out
