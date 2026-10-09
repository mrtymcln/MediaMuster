import json, os, subprocess, hashlib
from pathlib import Path
root=Path('/Users/martymclean/Developer/MediaMuster')
paths=[('/Volumes/EDIT/Avid MediaFiles/MXF/86452/msmMMOB.mdb',3),('/Volumes/EDIT/Avid MediaFiles/MXF/86452/V01.6A0416EF_15EDA15EDA552V copy.mxf',3),('/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/msmFMID.pmr',1),(str(root/'tests/fixtures/omf/avid_supporting/BLACK_1280x720x1_DNxHD_145.omf'),1),(str(root/'tests/fixtures/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav'),1),(str(root/'tests/fixtures/omf/mc2026_audio/TONE_100A01.6A972997.0C53E0.aif'),1),('/Users/martymclean/Downloads/sample.avb',1)]
def stamp(path):
 s=os.stat(path)
 return {'sizeBytes':s.st_size,'modifiedNs':str(s.st_mtime_ns),'device':str(s.st_dev),'inode':str(s.st_ino)}
records=[]
errors=[]
with open('/private/tmp/canon-chunk-archive-results.jsonl','w') as out:
 for path,trials in paths:
  for trial in range(trials):
   before=stamp(path)
   pair={}
   for mode in (['baseline','packed'] if trial%2==0 else ['packed','baseline']):
    p=subprocess.run(['/private/tmp/canon-chunk-archive-probe',mode,path],capture_output=True,text=True,env=dict(os.environ,QT_HASH_SEED='0'))
    if p.returncode: raise RuntimeError((path,mode,p.returncode,p.stderr))
    value=json.loads(p.stdout);value.update(trial=trial+1,sourceStamp=before,stderr=p.stderr)
    records.append(value);pair[mode]=value;out.write(json.dumps(value,ensure_ascii=False)+'\n');out.flush()
   if stamp(path)!=before:errors.append({'path':path,'error':'Source stamp changed'})
   for key in ['graphSha256','projectionSha256','objects','properties','relationships','serializedBytes']:
    if pair['baseline'][key]!=pair['packed'][key]:errors.append({'path':path,'trial':trial+1,'field':key})
   if not pair['packed']['archiveExact']:errors.append({'path':path,'error':'Archive digest mismatch'})
   print(json.dumps({'case':len(records)//2,'path':path,'trial':trial+1,'archiveBytes':pair['packed']['archivePayloadBytes'],'baselineHotFootprint':pair['baseline']['memoryGraphAndProjection'],'packedHotFootprint':pair['packed']['memoryArchiveAndProjection'],'packingMs':pair['packed']['packMs'],'errors':len(errors)}),flush=True)
receipt={'method':'Read-only bounded RAM-archive feasibility experiment. Fresh process per read, alternating pair order on repeated cases; current Debug Canon reader library linked to O2 arm64 C++17 diagnostic, Qt6.5.3. Streams the existing complete-field serialization into independent64KiB qCompress level1 blocks; never holds a full uncompressed serialization. Validates SHA256 of concatenated qUncompress bytes and retains projected metadata after graph release.','limitations':['Does not implement typed struct restoration or prove pointer-sharing topology. Does not verify sequence resolution after AVB restoration. AVB projection is empty in this diagnostic.','No production code changed. Not a full GUI, full scan, Windows/NEXIS, or300k workload test.','Memory readings are macOS physical footprint and resident peak, not Windows heap. Allocator retention can prevent released graph storage from immediately returning to OS.','Largest single source still parsed as expanded graph; packing adds CPU work. No guaranteed total RAM bound.'],'records':records,'errors':errors,'sourceSha256':hashlib.sha256(Path('/private/tmp/canon-chunk-archive-probe.cpp').read_bytes()).hexdigest(),'librarySha256':hashlib.sha256((root/'build-canon/libmediamuster_canon.a').read_bytes()).hexdigest(),'binarySha256':hashlib.sha256(Path('/private/tmp/canon-chunk-archive-probe').read_bytes()).hexdigest()}
Path('/private/tmp/canon-chunk-archive-receipt.json').write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n')
if errors: raise RuntimeError(errors)
