from pathlib import Path
import os,subprocess,json,time,hashlib
root=Path('/Users/martymclean/Developer/MediaMuster');base=Path('/private/tmp')
reference=json.loads((root/'Project Canon/evidence/property-compaction-before-real-scan-2026-10-08.json').read_text())
paths=sorted({row['path'] for row in reference['sources']});folders=sorted({str(Path(path).parent) for path in paths})
roots=reference['roots']
def stamps():
 result=[]
 for path in paths+folders:
  value=os.stat(path)
  result.append({'path':path,'device':str(value.st_dev),'inode':str(value.st_ino),'size':str(value.st_size),'mtimeNs':str(value.st_mtime_ns),'directory':path in folders})
 return result
initial=stamps();runs=[]
for mode in ['before','after']:
 pre=stamps();started=time.time()
 env=dict(os.environ,QT_HASH_SEED='0')
 process=subprocess.run([str(base/f'folder-cache-scan-proof-{mode}'),*roots],cwd=root,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (base/f'folder-cache-probe-{mode}.json').write_text(process.stdout)
 (base/f'folder-cache-probe-{mode}.stderr.txt').write_text(process.stderr)
 post=stamps()
 if process.returncode:raise RuntimeError(f'{mode} exited {process.returncode}; {process.stderr[:500]}')
 result=json.loads(process.stdout)
 runs.append({'mode':mode,'wallSeconds':time.time()-started,'returncode':process.returncode,'stderrBytes':len(process.stderr),'sourceAndFolderStampsStable':pre==post==initial})
 print(mode,result['rows'],'rows',result['callCounts']['canonicalFolderLookups'],'canonical calls',result['callCounts']['folderTimestampQueries'],'folder mtime calls',result['callCounts']['freshCheckUnchangedProbes'],'fresh safety probes',flush=True)
 if pre!=post or pre!=initial:raise RuntimeError('Inputs changed during/between proof scans')
before=json.loads((base/'folder-cache-probe-before.json').read_text());after=json.loads((base/'folder-cache-probe-after.json').read_text())
semanticKeys=['roots','rows','candidates','sources','discoveryIssues','reconciliationIssues','discoveryComplete','parsingComplete','reconciliationComplete','cancelled','hashes','fileDigests','sourceDigests','callbackCounts']
equivalent={key:before[key]==after[key] for key in semanticKeys}
checks={key:before['callCounts'][key]==after['callCounts'][key] for key in ['freshCheckUnchangedProbes','freshChecksByPath','freshChecksByReaderHint']}
summary={'baselineCommit':'2d96ac2','roots':roots,'sourceFileCount':len(paths),'folderCount':len(folders),'sourceAndFolderStampsStable':all(run['sourceAndFolderStampsStable'] for run in runs),'semanticEquivalence':equivalent,'unchangedSafetyProbes':checks,'hashes':before['hashes'],'beforeCounts':before['callCounts'],'afterCounts':after['callCounts'],'runs':runs,'sourceAndFolderStamps':initial,'definitions':{'canonicalFolderLookups':'Actual invocations of QFileInfo::canonicalFilePath in ScanEngine folderKey, after cancellation/cache decisions. Counts application calls; no inference about OS or network transactions.','folderTimestampQueries':'Actual existing QFileInfo(folder).lastModified invocations in ScanEngine database-status folder receipts. Counts application calls; no inference about OS or network transactions.','freshCheckUnchangedProbes':'Fresh QFileInfo(candidate.path) constructions inside checkUnchanged after already-changed early return. Each calls exists, then lastModified when present; no instrumentation filesystem queries added. ReaderHint 0 PMR, 1 MDB, 2 MXF, 3 legacy media.','scope':'Canon ScanEngine matching/reconciliation only; DiscoveryEngine and VolumeManager are outside counter scope. Identical direct ScanEngine request/roots, complete semantic hashes of final records, every graph, scheduling, issues and callbacks.','timing':'Instrumented processes and diagnostic allocations differ from application; timings and memory supplementary. Root runs uninstrumented trials separately.'}}
(base/'folder-cache-probe-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print('Proof complete; all semantic keys equivalent:',all(equivalent.values()),'; all per-path safety counts unchanged:',all(checks.values()),flush=True)
if not all(equivalent.values()) or not all(checks.values()):raise RuntimeError('Proof difference; inspect outputs')
