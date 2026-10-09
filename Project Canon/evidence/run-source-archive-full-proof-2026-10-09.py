from pathlib import Path
import os,subprocess,json,time,sys
base=Path('/private/tmp/source-archive-full-proof')
root=Path('/Users/martymclean/Developer/MediaMuster')
reference=json.load(open('/private/tmp/source-archive-before-real-scan-2026-10-09.json'))
paths=sorted({row['path'] for row in reference['sources']});folders=sorted({str(Path(path).parent) for path in paths})
def stamps():
 result=[]
 for path in paths+folders:
  value=os.stat(path)
  result.append({'path':path,'device':str(value.st_dev),'inode':str(value.st_ino),'size':str(value.st_size),'mtimeNs':str(value.st_mtime_ns),'directory':path in folders})
 return result
mode=sys.argv[1]
pre=stamps(); started=time.time()
process=subprocess.run([str(base/f'source-archive-full-proof-{mode}'),*reference['roots']],cwd=root,env=dict(os.environ,QT_HASH_SEED='0'),text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
(base/f'{mode}.json').write_text(process.stdout)
(base/f'{mode}.stderr.txt').write_text(process.stderr)
post=stamps()
receipt={'mode':mode,'roots':reference['roots'],'sourceCount':len(paths),'folderCount':len(folders),'wallSeconds':time.time()-started,'returncode':process.returncode,'sourceAndFolderStampsStable':pre==post,'sourceAndFolderStamps':pre}
(base/f'{mode}-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print({key:value for key,value in receipt.items() if key!='sourceAndFolderStamps'})
if process.returncode: print(process.stderr[:3000]);raise SystemExit(process.returncode)
if pre!=post:raise RuntimeError('Inputs changed within proof scan')
result=json.loads(process.stdout)
print({key:result[key] for key in ('rows','sources','sourceObjects','sourceRelationships','sourceProperties','originalValueBytes','scanMsInstrumented','hashMs','footprintRetained','peakResidentRetained','hashes')})
