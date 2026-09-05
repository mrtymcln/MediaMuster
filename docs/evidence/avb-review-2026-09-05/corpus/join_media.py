import pathlib,json,sys,collections
sys.path.insert(0,'/tmp/mediamuster-avb-review/corpus')
from compare import forms
OUT=pathlib.Path('/tmp/mediamuster-avb-review/corpus')
rows=json.loads(pathlib.Path('/tmp/mediamuster-audit/usage-identity-export-after.json').read_text())
results=json.loads((OUT/'comparison.json').read_text())[:42]
def form_ids(*ids):
    out=set()
    for value in ids:
        if not value: continue
        s=value.replace('.','')
        if len(s)!=64:continue
        out.update(forms(bytes.fromhex(s)))
    return out
joins=[]
for r in results:
    structured=form_ids(*r['structured_ids'])
    scanned=form_ids(*(h['id_le'] for h in r['scan_hits']))
    file_owners={raw:owners for raw,owners in r['structured_ids'].items() if any(o.get('descriptor_class') in ('CDCI','RGBA','PCMA','MPGA','MPGI','AIFC','WAVE') or o.get('mob_type')=='MasterMob' for o in owners)}
    material=form_ids(*file_owners)
    for row in rows:
        h=row.get('header',{}); m=row.get('mdbEssence',{})
        ids=form_ids(h.get('file'),h.get('master'),m.get('file'),row.get('mdbInferredMaster'))
        if not ids & material: continue
        joins.append(dict(bin=r['path'],media=row['path'],exists=pathlib.Path(row['path']).exists(),
                          matched_by_scavenger=bool(ids&scanned),name=row.get('mdbName') or h.get('name'),
                          mdb_usage=row.get('mdbUsage'),header_file=h.get('file'),header_master=h.get('master'),mdb_master=row.get('mdbInferredMaster'),
                          structured_id_owners={raw:owners for raw,owners in file_owners.items() if forms(bytes.fromhex(raw))&ids}))
(OUT/'media_joins.json').write_text(json.dumps(joins,indent=2))
missed=[r for r in joins if not r['matched_by_scavenger']]
print('Known media/bin pairs',len(joins),'missed',len(missed),'distinct missing files',len(set(r['media'] for r in missed)))
print('Missing MDB usage',dict(collections.Counter(r['mdb_usage'] for r in missed)))
print(json.dumps(missed[:2],indent=2))
