import struct, pathlib, re, json, hashlib
root=pathlib.Path('/private/tmp/mdvx-performance-audit')
p=pathlib.Path('/Applications/MDVx.app/Contents/MacOS/MDVx'); raw=p.read_bytes()
magic,n=struct.unpack_from('>II',raw,0)
for i in range(n):
 c,sub,off,size,align=struct.unpack_from('>IIIII',raw,8+20*i)
 if c==0x100000c: break
b=raw[off:off+size]; header=struct.unpack_from('<IiiIIIII',b,0); pos=32; secs=[]
for i in range(header[4]):
 cmd,sz=struct.unpack_from('<II',b,pos)
 if cmd==0x19:
  seg=struct.unpack_from('<II16sQQQQiiII',b,pos)
  for j in range(seg[-2]):
   ss=struct.unpack_from('<16s16sQQIIIIIIII',b,pos+72+80*j)
   secs.append((ss[0].strip(b'\0').decode(),ss[1].strip(b'\0').decode(),ss[2],ss[3],ss[4]))
 pos+=sz
def at(a,size):
 for name,seg,addr,ln,fo in secs:
  if addr<=a<addr+ln: return b[fo+a-addr:fo+a-addr+size]
 raise ValueError(hex(a))
def cf(a):
 isa,flags,ptr,n=struct.unpack('<QQQQ',at(a,32))
 return at(ptr,n*2 if flags&16 else n).decode('utf-16le' if flags&16 else 'utf-8',errors='replace')
def annotate(line):
 m=re.match(r'([0-9a-f]+)\s+adr\s+\w+, #(-?[0-9]+) ; Objc cfstring ref:',line)
 if m:
  a=int(m[1],16)+int(m[2]); value=cf(a)
  return line.split(' ;')[0]+' ; decoded cfstring '+hex(a)+': '+json.dumps(value,ensure_ascii=False)+'\n'
 return line
src=(root/'disassembly-arm64.txt').read_text().splitlines(True)
selected=['PMRFile','MDVxPMRScannerOperation','MDVxDatabaseItem','MDVxMediafile','MDVxOMFI','UIDtoBentoObj','MDVxRBTree','MDVxRBTree_u32t_u32t','MDVxRBTreeNode']
classes='|'.join(map(re.escape,selected))
active=False; dst=[]
for line in src:
 if line.rstrip().endswith(':') and not re.match(r'^[0-9a-f]{16}',line):
  active=bool(re.search(r'\[('+classes+r') ',line)) or line.startswith('_UID_to_bentotree_')
 if active: dst.append(annotate(line))
(root/'scanner-methods-arm64-annotated.txt').write_text(''.join(dst))
(root/'identity.json').write_text(json.dumps({'binary':str(p),'sha256':hashlib.sha256(raw).hexdigest(),'architectures':['x86_64','arm64'],'arm64_slice_offset':off,'arm64_slice_size':size,'bundle_version':'4073','short_version':'0.2'},indent=2)+'\n')
print('selected lines',len(dst))
for line in dst:
 if 'decoded cfstring' in line and ('0x10003a' in line or '0x100043a' in line): print(line,end='')
