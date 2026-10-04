import sys,os,json,collections,pathlib,time,contextlib,io
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
from avb import utils
ROOT=pathlib.Path('/Volumes/EDIT/2017_09_09_FIXING_JAMES_KINGSLEY/01_AVID/FIXING_JAMES_KINGSLEY')
FILES=[ROOT/'01_SEQ/01_SEQ.avb',ROOT/'01_SEQ/02_SEQ_LOCK.avb']
def mobkey(mobid):return bytes(mobid.bytes_le).hex() if mobid is not None else None
def desc(mob):
 raw=collections.OrderedDict.get(mob.property_data,'descriptor')
 if isinstance(raw,utils.AVBObjectRef) and raw.index:
  ch=mob.root.read_chunk(raw.index)
  return {'class':ch.class_id.decode('ascii','replace'),'object':raw.index}
 return None
def info(mob):
 return {'object':mob.instance_id,'name':getattr(mob,'name',None),'type':mob.mob_type,'usage':mob.usage_code,'id':str(mob.mob_id),'bytes_le':mobkey(mob.mob_id),'edit_rate':getattr(mob,'edit_rate',None),'length':getattr(mob,'length',None),'tracks':len(mob.tracks),'descriptor':desc(mob)}
def children(value):
 if isinstance(value,collections.OrderedDict):return list(collections.OrderedDict.items(value))
 if isinstance(value,dict):return list(value.items())
 if isinstance(value,(list,tuple)):return [(str(i),v) for i,v in enumerate(value)]
 if hasattr(value,'property_data'):return list(collections.OrderedDict.items(value.property_data))
 return []
def direct(mob):
 seen=set();stack=[('',mob)];refs=[];embedded=[];classes=collections.Counter();errors=[]
 while stack:
  path,x=stack.pop()
  if isinstance(x,utils.AVBObjectRef):
   try:x=x.value
   except Exception as e:errors.append({'path':path,'error':str(e)});continue
  if x is None or isinstance(x,(str,int,float,bytes,bytearray,bool)):continue
  ident=('obj',getattr(x,'instance_id')) if hasattr(x,'instance_id') else ('helper',id(x))
  if ident in seen:continue
  seen.add(ident)
  klass=getattr(x,'class_id',None)
  if klass:
   classes[klass.decode('ascii','replace')]+=1
   if klass==b'CMPO' and x is not mob:
    embedded.append({'path':path,'object':x.instance_id,'target':mobkey(x.mob_id),'name':getattr(x,'name',None)});continue
   if klass==b'SCLP':
    mid=getattr(x,'mob_id',None);key=mobkey(mid)
    refs.append({'object':x.instance_id,'path':path,'target':key,'id':str(mid) if mid else None,'track':getattr(x,'track_id',None),'kind':getattr(x,'media_kind',None),'start':getattr(x,'start_time',None),'length':getattr(x,'length',None),'edit_rate':getattr(x,'edit_rate',None)})
  try:child=children(x)
  except Exception as e:errors.append({'path':path,'error':repr(e)});continue
  for name,value in child:stack.append((path+'.'+str(name),value))
 return refs,embedded,dict(classes),errors
reports=[];allids=collections.defaultdict(list)
for file in FILES:
 before=os.stat(file);start=time.monotonic()
 with avb.open(str(file),use_ext=False) as f:
  original_read=f.read_object
  def quiet_read(index):
   with contextlib.redirect_stdout(io.StringIO()):return original_read(index)
  f.read_object=quiet_read
  chunks=collections.Counter();allmobs=[]
  for index in range(1,len(f.object_positions)):
   chunk=f.read_chunk(index);chunks[chunk.class_id.decode('ascii','replace')]+=1
   if chunk.class_id==b'CMPO':allmobs.append(f.read_object(index))
  byid=collections.defaultdict(list)
  for mob in allmobs:byid[mobkey(mob.mob_id)].append(mob)
  items=f.content.items;itemindexes={item.mob.instance_id for item in items}
  mobinfo={mob.instance_id:info(mob) for mob in allmobs}
  top=[mob for mob in allmobs if mob.mob_type_id==1 and mob.usage_code==0 and mob.instance_id in itemindexes]
  directbyid={};edges={};errors=[]
  for mob in allmobs:
   refs,embedded,classes,problems=direct(mob)
   directbyid[mob.instance_id]={'sourceClips':refs,'embeddedMobs':embedded,'classes':classes}
   targets=[]
   for ref in refs+embedded:
    key=ref['target'];ref['targets']=[m.instance_id for m in byid.get(key,[])]
    targets.extend(ref['targets'])
   edges[mob.instance_id]=list(set(targets));errors.extend(problems)
  topstats=[]
  for mob in top:
   visited=set();todo=[mob.instance_id]
   while todo:
    oid=todo.pop()
    if oid in visited:continue
    visited.add(oid);todo.extend(edges.get(oid,[]))
   refs=[ref for oid in visited for ref in directbyid[oid]['sourceClips']]
   missing=[r for r in refs if r['target'] and int(r['target'],16) and not r['targets']]
   nulls=[r for r in refs if r['target'] and not int(r['target'],16)]
   topstats.append({'mob':mobinfo[mob.instance_id],'directSourceClips':len(directbyid[mob.instance_id]['sourceClips']),'directTargetTypes':dict(collections.Counter(mobinfo[t]['type'] for r in directbyid[mob.instance_id]['sourceClips'] for t in r['targets'])),'reachableMobs':len(visited),'reachableMobTypes':dict(collections.Counter(mobinfo[x]['type'] for x in visited)),'reachableUsageTypes':dict(collections.Counter(mobinfo[x]['type']+':'+str(mobinfo[x]['usage']) for x in visited)),'reachableMobObjects':sorted(visited),'reachableSourceClipObjects':len({r['object'] for r in refs}),'missingSourceClipRefs':len(missing),'missingIds':sorted({r['target'] for r in missing}),'nullSourceClipRefs':len(nulls),'outsideBinItemObjects':sorted(visited-itemindexes)})
  allrefs=[ref for d in directbyid.values() for ref in d['sourceClips']]
  report={'path':str(file),'size':before.st_size,'byteOrder':f.ictx.byte_order,'chunkCounts':dict(chunks),'binItems':len(items),'userPlacedCounts':dict(collections.Counter(item.mob.mob_type+':'+str(item.mob.usage_code)+':'+str(item.user_placed) for item in items)),'allCompositionObjects':len(allmobs),'compositionObjectsOutsideBinItems':sorted(set(mobinfo)-itemindexes),'duplicateMobIds':{k:[m.instance_id for m in v] for k,v in byid.items() if len(v)>1},'mobCounts':dict(collections.Counter(m.mob_type+':'+str(m.usage_code) for m in allmobs)),'descriptors':dict(collections.Counter((desc(m)['class'] if desc(m) else 'None') for m in allmobs if m.mob_type_id==3)),'topSequences':topstats,'allMobs':list(mobinfo.values()),'directGraphs':directbyid,'errors':errors}
  for mob in allmobs:allids[mobkey(mob.mob_id)].append({'bin':str(file),'object':mob.instance_id,'name':getattr(mob,'name',None),'type':mob.mob_type,'usage':mob.usage_code})
 after=os.stat(file);report['seconds']=time.monotonic()-start;report['fileUnchangedDuringRead']=before.st_size==after.st_size and before.st_mtime_ns==after.st_mtime_ns
 reports.append(report)
 print(file.name,'mobs',len(allmobs),'items',len(items),'outsideitems',len(report['compositionObjectsOutsideBinItems']),'top',len(top),'errors',len(errors),'sec',round(report['seconds'],2),flush=True)
for r in reports:
 for top in r['topSequences']:
  top['crossBinMissingIdMatches']={mid:allids.get(mid,[]) for mid in top['missingIds'] if allids.get(mid)}
json.dump(reports,open('/tmp/canon-avb-sequence-results.json','w'),ensure_ascii=False,indent=2)
summary=[]
for r in reports:
 s={k:v for k,v in r.items() if k not in ['allMobs','directGraphs']}
 for top in s['topSequences']:top.pop('reachableMobObjects',None)
 summary.append(s)
json.dump(summary,open('/tmp/canon-avb-sequence-summary.json','w'),ensure_ascii=False,indent=2)
