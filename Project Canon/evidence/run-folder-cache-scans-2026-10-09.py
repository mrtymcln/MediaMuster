from pathlib import Path
import os,subprocess,time,json
root=Path('/Users/martymclean/Developer/MediaMuster')
evidence=root/'Project Canon/evidence'
records=[]
for pair,order in enumerate([['before','after'],['after','before'],['before','after']],1):
 for mode in order:
  stem=f'folder-cache-{mode}-repeat{pair}-2026-10-09'
  env=dict(os.environ,QT_HASH_SEED='0',MEDIAMUSTER_CANON_REAL_SCAN_ROOTS='/Users/Shared/AvidMediaComposer;/Volumes/EDIT',MEDIAMUSTER_CANON_REAL_SCAN_REPORT=str(evidence/(stem+'.json')),MEDIAMUSTER_CANON_REAL_SCAN_CSV=str(evidence/(stem+'.csv')))
  exe='/private/tmp/tst_scanner-before-folder-cache' if mode=='before' else str(root/'build-canon/tests/tst_scanner')
  start=time.time()
  run=subprocess.run([exe,'optional_read_only_real_scan'],cwd=root,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (evidence/(stem+'.txt')).write_text(run.stdout)
  if run.returncode:raise RuntimeError(f'{stem}: exit {run.returncode}; see log')
  report=json.loads((evidence/(stem+'.json')).read_text())
  records.append({'pair':pair,'order':order,'mode':mode,'stem':stem,'scanMs':report['scanMs'],'processWallSeconds':time.time()-start,'memoryRetained':report['memoryRetained']})
  print(stem,report['rows'],'rows',report['scanMs'],'ms',flush=True)
(evidence/'folder-cache-scan-trials-2026-10-09.json').write_text(json.dumps(records,indent=2)+'\n')
print('All six scan runs completed.',flush=True)
