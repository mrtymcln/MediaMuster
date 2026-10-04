import sys,json,collections,pathlib,os
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
base=pathlib.Path('/Volumes/EDIT/2017_09_09_FIXING_JAMES_KINGSLEY/01_AVID/FIXING_JAMES_KINGSLEY')
notes=pathlib.Path('/Users/martymclean/Library/Group Containers/group.com.apple.notes/Accounts/05F4B15D-DD7C-454C-B334-B1E4C72107E4/Media/D109B927-C69B-4EFA-AFB9-43FE3E91414C/1_DA583EA9-1B03-4A3E-A2A7-2C78C7E68095')
paths=[pathlib.Path('/Users/martymclean/Downloads/sample.avb'),pathlib.Path(json.load(open('Project Canon/evidence/non-english-encoding-specimens-2026-10-03.json'))['specimens'][2]['path']),base/'01_SEQ/01_SEQ.avb',base/'01_SEQ/02_SEQ_LOCK.avb',*sorted((base/'03_TRNSCDS').glob('*.avb'))]
assert len(paths)==9
rows=[]
for p in paths:
 before=p.stat()
 with avb.open(str(p),use_ext=False) as f:
  classes=collections.Counter();extent=0
  for index in range(1,len(f.object_positions)):
   ch=f.read_chunk(index);classes[ch.class_id.decode('ascii','replace')]+=1;extent=max(extent,ch.pos+ch.size)
  row={'path':str(p),'size':before.st_size,'byteOrder':f.ictx.byte_order,'chunks':len(f.object_positions)-1,'root':f.root_index,'rootClass':f.content.class_id.decode(),'binItems':len(f.content.items),'classes':dict(classes),'endOfLastChunk':extent}
 after=p.stat();row['sizeAndMtimeUnchanged']=before.st_size==after.st_size and before.st_mtime_ns==after.st_mtime_ns;rows.append(row)
json.dump(rows,open('/tmp/canon-avb-framing-inventory.json','w'),ensure_ascii=False,indent=2)
json.dump([str(p) for p in paths],open('/tmp/canon-avb-source-paths.json','w'),ensure_ascii=False,indent=2)
print(json.dumps([{k:v for k,v in row.items() if k!='classes'} for row in rows],indent=2,ensure_ascii=False))
