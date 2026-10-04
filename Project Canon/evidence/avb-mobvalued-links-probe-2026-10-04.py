import sys,collections,contextlib,io,json
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
from avb import utils
path='/Volumes/EDIT/2017_09_09_FIXING_JAMES_KINGSLEY/01_AVID/FIXING_JAMES_KINGSLEY/01_SEQ/01_SEQ.avb'
def rawitems(obj):
 if hasattr(obj,'property_data'):return list(collections.OrderedDict.items(obj.property_data))
 if isinstance(obj,collections.OrderedDict):return list(collections.OrderedDict.items(obj))
 if isinstance(obj,dict):return list(obj.items())
 if isinstance(obj,(list,tuple)):return list(enumerate(obj))
 return []
def refs(obj,path=''):
 if isinstance(obj,utils.AVBObjectRef):
  yield path,obj.index
  return
 for name,value in rawitems(obj):yield from refs(value,path+'.'+str(name))
with avb.open(path,use_ext=False) as f:
 classes={i:f.read_chunk(i).class_id.decode() for i in range(1,len(f.object_positions))}
 targets={i for i,c in classes.items() if c in ['MCMR','TMBC','APOS','ABOB','DIDP','MPGP','ASPI']}
 incoming=collections.defaultdict(list);mobids=collections.defaultdict(list);objects={};errors=[]
 for i,cl in classes.items():
  try:
   with contextlib.redirect_stdout(io.StringIO()):o=f.read_object(i)
  except Exception as e:continue
  if cl=='CMPO':mobids[bytes(o.mob_id.bytes_le).hex()].append({'object':i,'name':o.name,'mobtype':o.mob_type})
  if i in targets:objects[i]=o
  for field,target in refs(o):
   if target in targets:incoming[target].append({'object':i,'class':cl,'field':field})
 result=[]
 for i,o in objects.items():
  mid=bytes(o.mob_id.bytes_le).hex() if hasattr(o,'mob_id') else None
  result.append({'object':i,'class':classes[i],'id':mid,'mobTargets':mobids[mid] if mid else [],'parents':incoming[i]})
 json.dump(result,open('/private/tmp/avb-mobvalued-links-probe.json','w'),indent=2)
 for cl in ['MCMR','TMBC','APOS','ABOB','DIDP','MPGP','ASPI']:
  rows=[r for r in result if r['class']==cl]
  print(cl,'count',len(rows),'targetTypes',dict(collections.Counter(t['mobtype'] for r in rows for t in r['mobTargets'])),'parentPaths',dict(collections.Counter(p['class']+p['field'] for r in rows for p in r['parents'])))
  print(rows[:2])
