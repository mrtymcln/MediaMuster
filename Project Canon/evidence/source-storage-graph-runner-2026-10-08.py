import json,os,pathlib,subprocess,time
paths=json.load(open('/private/tmp/all-source-compaction-graph-paths.json'))
env=dict(os.environ,QT_HASH_SEED='0')
output='/private/tmp/all-source-compaction-graph-results.jsonl'
errors=[]
def stamp(path):
 s=os.stat(path)
 return {'path':path,'sizeBytes':s.st_size,'modifiedNs':str(s.st_mtime_ns),'device':str(s.st_dev),'inode':str(s.st_ino)}
with open(output,'w') as writer:
 for index,path in enumerate(paths):
  initial=stamp(path)
  pair={}
  for mode in ['before','after']:
   p=subprocess.run(['/private/tmp/all-source-compaction-graph-'+mode,path],capture_output=True,text=True,env=env)
   if p.returncode:
    errors.append({'path':path,'mode':mode,'returncode':p.returncode,'stderr':p.stderr})
    print('ERROR',mode,path,p.stderr,flush=True)
    break
   result=json.loads(p.stdout)
   result['mode']=mode
   result['stamp']=initial
   result['stderr']=p.stderr
   writer.write(json.dumps(result,ensure_ascii=False)+'\n'); writer.flush()
   pair[mode]=result
  if stamp(path)!=initial:
   errors.append({'path':path,'error':'Source stamp changed'})
  if len(pair)==2:
   fields=['graphSha256','objects','relationships','properties','encodingBytes','outcome','container','graphSerializedBytes','projectionSerializedBytes']
   fields += ['avbResolutionSha256','sequenceCandidates','wholeBinRoots','wholeBinEdges','wholeBinMedia','wholeBinIssues','wholeBinComplete','selectedRoots','selectedEdges','selectedMedia','selectedIssues','selectedComplete'] if path.lower().endswith('.avb') else ['projectionSha256','projectedFiles','projectedMasters']
   differences={key:[pair['before'][key],pair['after'][key]] for key in fields if pair['before'][key]!=pair['after'][key]}
   if differences: errors.append({'path':path,'differences':differences})
   for name,storage in pair['before']['listStorage'].items():
    updated=pair['after']['listStorage'][name]
    if storage['count']!=updated['count'] or storage['elementBytes']!=updated['elementBytes']:
     errors.append({'path':path,'storageCountChanged':name})
  if index%10==0 or index==len(paths)-1:
   print(index+1,'/',len(paths),'completed;',len(errors),'errors;',path,flush=True)
json.dump(errors,open('/private/tmp/all-source-compaction-graph-errors.json','w'),ensure_ascii=False,indent=2)
print('DONE',len(paths),'sources;',len(errors),'errors;',output,flush=True)
raise SystemExit(bool(errors))
