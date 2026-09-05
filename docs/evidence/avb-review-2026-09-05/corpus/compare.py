import sys, pathlib, json, collections, hashlib, subprocess, struct, contextlib, io
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
from avb.mobid import MobID

OUT=pathlib.Path('/tmp/mediamuster-avb-review/corpus')
PREFIX=bytes.fromhex('060a2b340101010101010f0013000000')
SUFFIX=bytes.fromhex('060e2b347f7f2a80')
def swap(b): return b[:16]+b[16:20][::-1]+b[20:22][::-1]+b[22:24][::-1]+b[24:]
def forms(b): return {b.hex(),swap(b).hex()}
def is_nil(b): return not any(b) or (b[:16]==PREFIX and not any(b[16:24]) and b[24:]==SUFFIX)
def decode_tagged(data,byte_order):
    # Prototype only: full 49-byte typed MobID envelope inside an indexed chunk.
    # A shipping parser should resolve class fields rather than search opaque payloads.
    bo='<' if byte_order=='little' else '>'
    first=b'\x41'+struct.pack(bo+'I',12)
    found=[]; pos=0
    while (pos:=data.find(first,pos))>=0:
        b=data[pos:pos+49]
        if len(b)==49 and all(b[k]==0x44 for k in (17,19,21,23)) and b[25]==0x48 and b[30]==0x46 and b[33]==0x46 and b[36:41]==b'\x41'+struct.pack(bo+'I',8):
            raw=b[5:17]+bytes(b[k] for k in (18,20,22,24))+b[26:30]+b[31:33]+b[34:36]+b[41:49]
            if byte_order=='big': raw=swap(raw)
            found.append(dict(offset=pos,id_le=raw.hex(),nil=is_nil(raw)))
        pos+=1
    return found
def scavenger(data):
    found=set(); hits=[]; i=0; n=len(data)
    while i+8<=n:
        i=data.find(b'060',i)
        if i<0 or i+8>n: break
        if data[i+3:i+8].lower() not in (b'a2b34',b'e2b34'):
            i+=1; continue
        j=i+8
        while j<min(i+76,n) and data[j] in b'0123456789abcdefABCDEF-': j+=1
        if j-i>=64:
            clean=data[i:j].replace(b'-',b'')
            if len(clean)==64:
                raw=bytes.fromhex(clean.decode())
                found.update(forms(raw)); hits.append(dict(offset=i,kind='text',id_le=raw.hex()))
            i=j
        else: i+=1
    k=0
    while k+32<=n:
        k=data.find(PREFIX,k)
        if k<0 or k+32>n: break
        if data[k+24:k+32]==SUFFIX:
            raw=data[k:k+32]; found.update(forms(raw)); hits.append(dict(offset=k,kind='omf_raw',id_le=raw.hex())); k+=32
        else: k+=1
    return found,hits

def compare(path):
    data=path.read_bytes(); scanned,hits=scavenger(data)
    result=dict(path=str(path),size=len(data),sha256=hashlib.sha256(data).hexdigest(),scan_forms=len(scanned),scan_hits=hits)
    ids=collections.defaultdict(list); classes=collections.Counter(); failures=[]; mob_types=collections.Counter(); names=[]; tag_hits=[]; nil_ids=collections.defaultdict(list)
    try:
        with avb.open(str(path),use_ext=False) as f:
            result.update(writer=f.creator_version,byte_order=f.ictx.byte_order,objects=len(f.object_positions)-1,root_index=f.root_index)
            for idx in range(1,len(f.object_positions)):
                chunk=f.read_chunk(idx); cls=chunk.class_id.decode(errors='replace'); classes[cls]+=1
                for h in decode_tagged(chunk.read(),f.ictx.byte_order):
                    h['offset']+=chunk.pos; h.update(chunk=cls,object_index=idx); tag_hits.append(h)
                for h in hits:
                    if chunk.pos<=h['offset']<chunk.pos+chunk.size: h.update(chunk=cls,object_index=idx,payload_offset=chunk.pos)
                try:
                    with contextlib.redirect_stdout(io.StringIO()): obj=f.read_object(idx)
                except Exception as e:
                    failures.append(dict(object_index=idx,chunk=cls,error=str(e))); continue
                if hasattr(obj,'mob_id'):
                    raw=bytes(obj.mob_id.bytes_le)
                    owner=dict(object_index=idx,chunk=cls,payload_offset=chunk.pos)
                    for key in ('name','mob_type','usage_code','media_kind','length','track_id'):
                        if hasattr(obj,key): owner[key]=getattr(obj,key)
                    descriptor=collections.OrderedDict.get(obj.property_data,'descriptor')
                    if descriptor is not None and getattr(descriptor,'index',0)>0: owner['descriptor_class']=f.read_chunk(descriptor.index).class_id.decode(errors='replace')
                    if is_nil(raw): nil_ids[raw.hex()].append(owner); continue
                    ids[raw.hex()].append(owner)
                    if hasattr(obj,'mob_type'): mob_types[obj.mob_type]+=1
                if cls=='CMPO' and getattr(obj,'name',None): names.append(dict(name=obj.name,mob_id=bytes(obj.mob_id.bytes_le).hex() if hasattr(obj,'mob_id') else None))
            result['bin_items']=[]
            for item in f.content.items:
                try:
                    mob=item.mob
                    result['bin_items'].append(dict(id_le=bytes(mob.mob_id.bytes_le).hex(),name=mob.name,mob_type=mob.mob_type,usage_code=mob.usage_code))
                except Exception as e: failures.append(dict(bin_item_error=str(e)))
    except Exception as e:
        result['error']=str(e)
    structured_forms=set()
    for raw in ids: structured_forms.update(forms(bytes.fromhex(raw)))
    result.update(structured_ids=dict(ids),nil_ids=dict(nil_ids),tagged_hits=tag_hits,classes=dict(classes),mob_types=dict(mob_types),object_failures=failures,
                  missed_ids={raw:owners for raw,owners in ids.items() if not forms(bytes.fromhex(raw))&scanned},
                  scan_ids_without_structured_owner=[h for h in hits if not forms(bytes.fromhex(h['id_le']))&structured_forms])
    tag_ids={h['id_le'] for h in tag_hits if not h['nil']}
    result.update(tagged_unique_ids=len(tag_ids),tagged_missing_from_structured=sorted(tag_ids-set(ids)),structured_missing_from_tagged=sorted(set(ids)-tag_ids))
    exe=OUT/'avb_probe'
    if exe.exists():
        proc=subprocess.run([str(exe),str(path)],capture_output=True,text=True,check=True)
        actual=json.loads(proc.stdout)[0]
        actual_ids={i.replace('.','') for i in actual['mobIds']}
        result.update(cpp_valid=actual['valid'],cpp_forms=len(actual_ids),cpp_vs_python_scan_agree=actual_ids==scanned)
    return result

if __name__=='__main__':
    paths=sorted(pathlib.Path('/Users/martymclean/Downloads/pyavb-main/tests/test_files').rglob('*.avb'))
    paths+=sorted(pathlib.Path('/Users/martymclean/Developer/MediaMuster/tests/fixtures/omf/mc2026_audio/bins').glob('*.avb'))
    paths+=sorted(pathlib.Path('/Users/martymclean/Documents/Avid Projects').rglob('*.avb'))
    if len(sys.argv)>1: paths += [pathlib.Path(p) for p in sys.argv[1:]]
    results=[compare(p) for p in paths]
    (OUT/'comparison.json').write_text(json.dumps(results,indent=2,default=str))
    for r in results:
        print(pathlib.Path(r['path']).name,'objects',r.get('objects'),'ids',len(r['structured_ids']),'missed',len(r['missed_ids']),'unowned_scan_hits',len(r['scan_ids_without_structured_owner']),'errors',len(r['object_failures']),r.get('error',''))
