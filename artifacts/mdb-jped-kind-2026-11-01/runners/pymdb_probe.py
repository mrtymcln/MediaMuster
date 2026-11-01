import sys, json, pathlib, datetime, gzip, hashlib, traceback
sys.path.insert(0, '/tmp/mediamuster-mdb-fresh-20261101/online/pymdb/src')
import mdb
from mdb.core import MDBObject, UnresolvedRef
from mdb.datatypes import Opaque, Rational, SMPTELabel
from mdb.mobid import MobID, ShortUID

OUT=pathlib.Path('/tmp/mediamuster-mdb-jped-20261101/online/pymdb-results')
OUT.mkdir(exist_ok=True)
def norm(x):
    if x is None or isinstance(x,(str,int,float,bool)): return x
    if isinstance(x,(MobID,ShortUID)): return {'type':type(x).__name__, 'hex':x.bytes.hex(), 'display':str(x)}
    if isinstance(x,Rational): return {'numerator':x.numerator,'denominator':x.denominator}
    if isinstance(x,(Opaque,SMPTELabel)): return {'type':type(x).__name__, 'hex':x.raw.hex(), 'type_name':getattr(x,'type_name',None)}
    if isinstance(x,(bytes,bytearray)): return {'hex':bytes(x).hex()}
    if isinstance(x,datetime.datetime): return x.isoformat()
    if isinstance(x,(MDBObject,UnresolvedRef)): return {'object_id':x.object_id,'class':getattr(x,'class_name',None)}
    if isinstance(x,dict):return {str(k):norm(v) for k,v in x.items()}
    if isinstance(x,(list,tuple)):return [norm(v) for v in x]
    return repr(x)
def safecall(fn):
    try:return norm(fn())
    except Exception as e:return {'error':type(e).__name__+': '+str(e)}
def props(obj):return {k:{'type':obj.type_of(k),'values':safecall(lambda:obj.values(k))} for k in obj.property_names()}
def tree(obj, depth=0, seen=None):
    if obj is None:return None
    if not isinstance(obj,MDBObject):return norm(obj)
    seen=set() if seen is None else seen
    result={'object_id':obj.object_id,'class':obj.class_name,'properties':props(obj)}
    if obj.object_id in seen or depth>8:return result
    seen.add(obj.object_id)
    result['references']=[tree(v,depth+1,seen) for v in obj.references()]
    return result

manifest=json.loads(pathlib.Path('/tmp/mediamuster-mdb-jped-20261101/corpus/inputs.json').read_text())
summary=[]
for item in manifest:
    if not item['avid_bento']:continue
    sha=item['sha256']; path=item['path']
    try:
        with mdb.open(path) as f:
            live={x.object_id for x in f.spine_mobs}
            result={'path':path,'sha256':sha,'reader_commit':'04380153a98580b7097de30520ed5a05005aaf2d','summary':norm(f.summary()),'header':tree(f.header,8),'live_object_ids':sorted(live),'mobs':[]}
            for m in f.mobs:
                row={'object_id':m.object_id,'live':m.object_id in live,'mob_id':norm(m.mob_id),'name':m.name,'edit_rate':norm(m.edit_rate),'group_length':m.group_length,'usage_code':m.usage_code,'properties':props(m),'attributes':safecall(m.attribute_dict),'paths':safecall(m.paths),'volumes':safecall(m.volumes),'bins':safecall(m.bins),'tracks':safecall(lambda:[tree(t) for t in m.tracks or []]),'descriptor':safecall(lambda:tree(m.descriptor))}
                result['mobs'].append(row)
            result['all_mobs']=len(result['mobs']);result['live_mobs']=len(live);result['physical_mobs']=sum(bool(x['descriptor']) for x in result['mobs']);result['live_physical_mobs']=sum(bool(x['descriptor']) and x['live'] for x in result['mobs'])
            with gzip.open(OUT/(sha+'.json.gz'),'wt') as g:json.dump(result,g,ensure_ascii=False)
            row={k:result[k] for k in ['path','sha256','all_mobs','live_mobs','physical_mobs','live_physical_mobs']}; summary.append(row); print(json.dumps(row),flush=True)
    except Exception as e:
        row={'path':path,'sha256':sha,'error':type(e).__name__+': '+str(e),'traceback':traceback.format_exc()}; summary.append(row);print(json.dumps(row),flush=True)
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2))
