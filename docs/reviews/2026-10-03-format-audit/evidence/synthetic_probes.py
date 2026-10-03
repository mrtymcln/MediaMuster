from pathlib import Path
import struct,json,subprocess
D=Path('/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-10-03-format-audit/evidence/probes');D.mkdir(exist_ok=True)
def b(h):return bytes.fromhex(h)
def u(n):return struct.pack('>I',n)
def ber(n):return bytes([n]) if n<128 else bytes([0x80+(n.bit_length()+7)//8])+n.to_bytes((n.bit_length()+7)//8,'big')
def field(t,v):return struct.pack('>HH',t,len(v))+v
def uid(n):return n.to_bytes(16,'big')
def pkg(n):return b('060a2b340101010501010f2013000000')+uid(n)
def set_(t,fields):
 key=bytearray(b('060e2b34025301010d01010101010000'));key[14]=t;v=b''.join(field(k,v) for k,v in fields.items());return bytes(key)+ber(len(v))+v
def arr(*ids):return u(len(ids))+u(16)+b''.join(ids)
def text(s):return s.encode('utf-16be')
def descriptor(**extra):
 f={0x3c0a:uid(3),0x3203:u(1920),0x3202:u(1080),0x3001:u(25)+u(1),0x3002:(250).to_bytes(8,'big'),0x320c:b'\0',0x3301:u(10),0x3201:b('060e2b340401010a0401020271030000')};f.update({int(k,16):v for k,v in extra.items()});return set_(0x28,f)
def base(fileuid=uid(2),component=None):
 data=set_(0x36,{0x3c0a:uid(1),0x4401:pkg(1),0x4402:text('UNRELATED_MASTER'),0x4403:arr(uid(4))})
 data+=set_(0x37,{0x3c0a:fileuid,0x4401:pkg(2),0x4402:text('REAL_FILE'),0x4701:uid(3)})
 data+=descriptor();data+=set_(0x3b,{0x3c0a:uid(4),0x4803:uid(5),0x4801:u(1),0x4b01:u(25)+u(1)})
 data+=set_(0x0f,{0x3c0a:uid(5),0x0201:component or b('060e2b34040101010103020202000000'),0x0202:(250).to_bytes(8,'big'),0x1001:arr()})
 return data
probes={
 'no_false_edge':base(),
 'false_UL_edge':base(fileuid=b('060e2b34040101010103020202000000')),
 'explicit_display_crop':descriptor(**{'3202':u(1088),'3208':u(1000),'3209':u(1800)}),
 'stored_1088_uncropped':descriptor(**{'3202':u(1088)}),
 'nearby_25_rate':descriptor(**{'3001':u(24995)+u(1000)}),
 'odd_5_byte_duration':descriptor(**{'3002':(250).to_bytes(5,'big')}),
 'oversized_20_byte_coding_UL':descriptor(**{'3201':b('060e2b340401010a0401020271030000')+b'JUNK'}),
}
paths=[]
for k,v in probes.items():p=D/(k+'.mxf');p.write_bytes(v);paths.append(str(p))
r=subprocess.run(['/tmp/mediamuster-audit-harness/build/audit'],input='\n'.join(paths)+'\n',text=True,capture_output=True,check=True);(D/'results.jsonl').write_text(r.stdout);print(r.stdout)
