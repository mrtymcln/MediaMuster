import json,gzip,pathlib,collections,fractions
base=pathlib.Path('/tmp/mediamuster-mdb-jped-20261101');results=[]
def key(s):return s[8:24]if len(s)==24 else s[32:48]if len(s)==64 else s
def rat(v):
 if not v:return None
 if isinstance(v,(int,float)):return str(fractions.Fraction(v))
 if isinstance(v,str):return None
 if isinstance(v,dict) and 'n'not in v:return None
 a,b=(v['n'],v['d'])if isinstance(v,dict)else v
 return str(fractions.Fraction(a,b))if b else None
for x in json.load(open(base/'corpus/inputs.json')):
 if not x['avid_bento']:continue
 sha=x['sha256'];b=json.load(open(base/'current/results'/f'{sha}.bella.json'));m=json.load(gzip.open(base/'binary/runtime'/f'{sha}.mobs.json.gz','rt'))['mobs'];mp=collections.defaultdict(list)
 for v in m:
  if 'mob_id_raw_hex'not in v:continue
  for role,tn in [('files','SourceMobsTree'),('masters','CompositionMobsTree')]:
   if v.get('native_head_roles',{}).get(tn,{}).get('is_this_object'):mp[(role,key(v['mob_id_raw_hex']))].append(v)
 r={'path':x['path'],'sha256':sha,'native_all_mob_count':len(m),'bella_files':len(b['files']),'bella_masters':len(b['masters']),'native_live_key_collisions':[{ 'role':k[0],'key':k[1],'objects':[z['object_id']for z in v]}for k,v in mp.items()if len(v)>1]};diffs=[];matched=collections.Counter();total=collections.Counter();unavailable=collections.Counter();join=collections.Counter()
 for role,vs in [('files',b['files']),('masters',b['masters'])]:
  for v in vs:
   nn=mp.get((role,key(v['id'])),[])
   if len(nn)!=1:
    diffs.append({'role':role,'field':'identity_join','bella_id':v['id'],'native_matches':len(nn)});continue
   join[role]+=1;z=nn[0];native=z.get('native_addInfoToItem',{});pairs=[]
   if role=='masters':pairs.append(('clipName',v['clipName'],z.get('native_string')))
   else:
    pairs.append(('project',v['project']or None,z.get('native_project')or None))
    if v.get('compression',{}).get('legacy')is not None:pairs.append(('stored_legacy_fourcc',v['compression']['legacy'],native.get('compression_fourcc_hex')))
    if v.get('compression',{}).get('resolutionId')is not None:pairs.append(('resolution_id',v['compression']['resolutionId'],native.get('DB_DIDResolutionID')))
    bd=v.get('bitDepth',{})
    if bd:
     raw=bd.get('rawComponentWidth');cbits=native.get('DB_bits');expect=raw if raw is not None else bd.get('bits')
     if cbits:pairs.append(('raw_bit_depth_or_width',expect,cbits))
     else:unavailable['files.bit_depth']+=1
    if v['kind']==1:
     a=rat(v.get('audioFrequency'));bb=rat(native.get('DB_samplerate'))
     if a is not None and bb is not None:pairs.append(('audio_frequency',a,bb))
     else:unavailable['files.audio_frequency']+=1
   for field,bv,nv in pairs:
    total[role+'.'+field]+=1
    if bv==nv:matched[role+'.'+field]+=1
    else:diffs.append({'role':role,'field':field,'bella_id':v['id'],'native_object_id':z['object_id'],'bella':bv,'native':nv})
 r['identity_joins']=dict(join);r['comparison_totals']=dict(total);r['matching_counts']=dict(matched);r['native_unavailable']=dict(unavailable);r['diffs']=diffs;results.append(r)
json.dump(results,open(base/'binary/native-vs-bella.json','w'),indent=2,ensure_ascii=False)
for r in results:print(r['path'],r['identity_joins'],'counts',r['comparison_totals'],'match',r['matching_counts'],'unavailable',r['native_unavailable'],'diffs',len(r['diffs']))
print('examples',[(r['path'],d)for r in results for d in r['diffs']][:12])
