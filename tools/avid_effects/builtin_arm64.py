"""Bounded symbolic name/category extraction for the hash-pinned 26.8 ARM64 routine.

This does not load or execute Avid code. External text conversions are identity
operations for the English source strings; feature gates are supplied booleans.
The caller verifies the binary hash before importing this module.
"""
import re,struct,json,collections,os
from pathlib import Path
B=Path(os.environ['AVID_EFFECT_LIBAME']).read_bytes()
segs=[];pos=32
for i in range(struct.unpack_from('<I',B,16)[0]):
 cmd,n=struct.unpack_from('<II',B,pos)
 if cmd==0x19:
  vm,sz,off,filesz=struct.unpack_from('<QQQQ',B,pos+24);segs.append((vm,sz,off,filesz))
 pos+=n
def raw(addr,n):
 for vm,sz,off,fsize in segs:
  if vm<=addr and addr+n<=vm+fsize:return B[off+addr-vm:off+addr-vm+n]
  if vm+fsize<=addr and addr+n<=vm+sz:return bytes(n)
 raise ValueError(('badaddr',hex(addr),n))
def string(addr):
 data=raw(addr,150);return data.split(b'\0',1)[0].decode('utf8')
I={}
for l in Path(os.environ['AVID_EFFECT_BUILTIN_ASM']).read_text().splitlines():
 m=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){3}[0-9a-f]{2}\s+([^\s]+)\s*(.*)',l)
 if m:I[int(m[1],16)]=(m[2],m[3].split(';')[0].strip(),m[3],l)
SETUP={}
for block in Path(os.environ['AVID_EFFECT_HELPER_ASM']).read_text().split('\n__'):
 name=block.split(':')[0].strip().lstrip('_');m=re.search(r'literal pool for: "([^"]+)"',block)
 if m:SETUP['__'+name]=m[1]
OBJ=0x900000000;VT=0xA00000000;FN=0xB00000000;FIRST=0xC00000000

def run(ident,alpha,first=0,other=0):
 regs={'x0':OBJ,'x1':ident,'x2':1,'sp':0xD00001000};mem={OBJ:VT,FIRST:first};cmp=(0,0,64);pc=min(I);out={'id':ident,'alphaFlex':alpha,'mcFirst':first,'otherToggle':other};steps=0
 def get(r):
  r=r.strip()
  if r in ['wzr','xzr']:return 0
  if r.startswith('#'):return int(r[1:],0)
  if r.startswith('0x'):return int(r,0)
  val=regs.get('x'+r[1:] if r.startswith('w') else r,0)
  return val&0xffffffff if r.startswith('w') else val
 def put(r,v):regs['x'+r[1:] if r.startswith('w') else r]=v& (0xffffffff if r.startswith('w') else 0xffffffffffffffff)
 def cond(c):
  a,b,bits=cmp;mask=(1<<bits)-1;a&=mask;b&=mask
  sa=a-(1<<bits) if a>>(bits-1) else a;sb=b-(1<<bits) if b>>(bits-1) else b
  return {'eq':a==b,'ne':a!=b,'le':sa<=sb,'gt':sa>sb,'hi':a>b,'ls':a<=b}[c]
 while pc in I:
  steps+=1
  if steps>500:raise ValueError(('too many',out,hex(pc)))
  op,args,detail,line=I[pc];a=[x.strip() for x in args.split(',')];nxt=pc+4
  if op=='mov':put(a[0],get(a[1]))
  elif op=='movk':put(a[0],get(a[0])|(get(a[1])<<int(a[2].split('#')[1])))
  elif op=='sxth':v=get(a[1])&65535;put(a[0],v-65536 if v&32768 else v)
  elif op in ['add','sub']:
   rhs=get(a[2]);rhs<<=int(a[3].split('#')[1]) if len(a)>3 else 0
   put(a[0],get(a[1])+rhs if op=='add' else get(a[1])-rhs)
  elif op=='adrp':put(a[0],(pc&~4095)+int(a[1])*4096)
  elif op=='adr':put(a[0],pc+get(a[1]))
  elif op=='cmp':cmp=(get(a[0]),get(a[1]),32 if a[0].startswith('w') else 64)
  elif op=='cset':put(a[0],int(cond(a[1])))
  elif op in ['csel','csinc']:put(a[0],get(a[1]) if cond(a[3]) else get(a[2])+(op=='csinc'))
  elif op=='b':nxt=int(a[0],0)
  elif op.startswith('b.'):
   if cond(op[2:]):nxt=int(a[0],0)
  elif op=='cbz':
   if get(a[0])==0:nxt=int(a[1],0)
  elif op=='br':nxt=get(a[0])
  elif op in ['ldr','ldrb','ldrh']:
   src=re.search(r'\[([^]]+)\]',args)[1].split(',');addr=get(src[0]);off=get(src[1]) if len(src)>1 else 0
   if len(src)>2:off<<=int(src[2].split('#')[1])
   addr+=off
   if VT<=addr<VT+0x1000:v=FN+addr-VT
   elif addr in mem:v=mem[addr]
   elif addr>=OBJ:v=0
   else:v=int.from_bytes(raw(addr,1 if op=='ldrb' else 2 if op=='ldrh' else 4 if a[0].startswith('w') else 8),'little')
   put(a[0],v)
  elif op in ['stp','ldp','str','stur','strh','strb','sturh']:pass
  elif op=='bl':
   if 'SetFundementals' in detail:out.update(identifier=string(get('x1')),name=string(get('x2')),fundamentalsAt=hex(pc))
   elif 'stx_func' in detail or 'AvTextcv' in detail:pass
   elif 'GetDynamicToggle' in detail:put('x0',alpha)
   elif 'GetMCFirstRef' in detail:put('x0',FIRST)
   elif 'FTF_GetToggle' in detail:put('x0',other)
   elif 'Setup' in detail:
    key=re.search(r'(__ZN\w+)',detail)[1]
    if key in SETUP:out.update(category=SETUP[key],categoryHelper=key)
   else:raise ValueError(('unknowncall',line))
  elif op=='blr':
   fun=get(a[0])-FN
   if fun==0x6f0:out.update(category=string(get('x1')),categoryAt=hex(pc));out.pop('categoryHelper',None)
   elif fun==0x6f8:out.update(name=string(get('x1')),nameAt=hex(pc))
   elif fun not in (0x6e8,0x6d8,0x730,0x6e0,0x700,0x708,0x738):raise ValueError(('unknownvcall',hex(fun),out,line))
  elif op=='ret':return out
  else:raise ValueError(('unknowninstruction',line))
  pc=nxt
 raise ValueError(('outside',hex(pc),out))
