from pathlib import Path
import json,gzip,struct,collections
out=Path(__file__).resolve().parent;data=json.loads((out/'comparison.json').read_text());old={r['path']:r['report']for r in json.load(gzip.open(out/'results.json.gz','rt'))};cache={};rows=[]
def objects(path):
 if path in cache:return cache[path]
 d=Path(path).read_bytes();off,n=struct.unpack_from('<II',d,len(d)-8);values=[];continued=False
 for i in range(off,off+n,24):
  o,p,t,ptr,size,g,flags=struct.unpack_from('<IIIIIHH',d,i);v=d[i+12:i+12+size]if flags&1 else d[ptr:ptr+size]
  if continued:values[-1][3]+=v
  else:values.append([o,p,t,v])
  continued=bool(flags&2)
 names={r[0]:r[3].rstrip(b'\0').decode('ascii','replace')for r in values if r[1]==24 and r[2]==21};os={}
 for o,p,t,v in values:os.setdefault(o,{}).setdefault(names.get(p,str(p)),[]).append(v)
 cache[path]=os;return os
norm=lambda v:v.replace('.','').lower()
for row in data['differences']:
 if row['path'].startswith('/private/tmp/') or '/.codex/' in row['path']:continue
 if row['field']=='Project':row['classification']='Bella approved owner/key selection chooses one retained OldScanner observation';row['selectedValuePresentAmongOldObservations']=row['bella'][0]in row['oldscanner']
 elif row['field']=='Precompute Category':
  m=next(m for m in old[row['path']]['masters']if row['identity']in [norm(i)for i in m['masterIds']]);owner=int(m['fields']['MasterMobId'][0]['object'].split(':')[1]);os=objects(row['path']);refs=os[owner].get('OMFI:CPNT:Attributes',[]);target=int.from_bytes(refs[0][:4],'little')if refs else None;has_list=target in os and bool(os[target].get('OMFI:ATTR:AttrRefs'));row['classification']='Bella treats existing ATTR without AttrRefs as empty; OldScanner conservatively withholds category';row['ownerObject']=owner;row['attributeListObject']=target;row['attributeListHasAttrRefs']=has_list
 rows.append(row)
r={'specimenDifferenceCount':len(rows),'reasonCounts':dict(collections.Counter(r['classification']for r in rows)),'allProjectSelectionsPresentInOldObservations':all(r['selectedValuePresentAmongOldObservations']for r in rows if r['field']=='Project'),'allCategoryDifferencesHaveExistingAttrWithoutAttrRefs':all(r['attributeListObject']is not None and not r['attributeListHasAttrRefs']for r in rows if r['field']=='Precompute Category'),'rows':rows}
(out/'classified-differences.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items()if k!='rows'},indent=2))
