import json,os,subprocess
paths=json.load(open('/private/tmp/all-source-compaction-graph-paths.json'))
path=max((p for p in paths if p.lower().endswith('.avb')),key=os.path.getsize)
env=dict(os.environ,QT_HASH_SEED='0')
def stamp(path):
 s=os.stat(path)
 return {'path':path,'sizeBytes':s.st_size,'modifiedNs':str(s.st_mtime_ns),'device':str(s.st_dev),'inode':str(s.st_ino)}
initial=stamp(path)
errors=[]
rows=[]
with open('/private/tmp/all-source-compaction-avb-repeat-results.jsonl','w') as writer:
 for pair,order in enumerate([['before','after'],['after','before'],['before','after']],1):
  current={}
  for mode in order:
   completed=subprocess.run(['/private/tmp/all-source-compaction-graph-'+mode,path],capture_output=True,text=True,env=env)
   if completed.returncode: raise RuntimeError(mode+': '+completed.stderr)
   result=json.loads(completed.stdout)
   result.update({'mode':mode,'pair':pair,'order':order,'stamp':initial,'stderr':completed.stderr})
   current[mode]=result
   rows.append(result)
   writer.write(json.dumps(result,ensure_ascii=False)+'\n');writer.flush()
  assert stamp(path)==initial
  for key in ['graphSha256','avbResolutionSha256','objects','properties','relationships','outcome','container','encodingBytes','sequenceCandidates','wholeBinRoots','wholeBinEdges','wholeBinMedia','wholeBinIssues','wholeBinComplete','selectedRoots','selectedEdges','selectedMedia','selectedIssues','selectedComplete']:
   if current['before'][key]!=current['after'][key]: errors.append({'pair':pair,'field':key})
  print('AVB repeat pair',pair,'finished; before/after parse footprint',current['before']['footprintParsed'],current['after']['footprintParsed'],'peak',current['before']['peakResidentParsed'],current['after']['peakResidentParsed'],flush=True)
json.dump(errors,open('/private/tmp/all-source-compaction-avb-repeat-errors.json','w'),indent=2)
assert not errors
assert all(not r['stderr'] for r in rows)
print('All three AVB repeat pairs semantically identical, unchanged source stamp, no stderr.',flush=True)
