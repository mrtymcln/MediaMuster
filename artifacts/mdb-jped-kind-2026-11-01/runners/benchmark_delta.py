import json,os,subprocess,re,statistics
from pathlib import Path
base=Path('/tmp/mediamuster-mdb-jped-20261101');inputs=[r for r in json.loads((base/'corpus/inputs.json').read_text()) if r['avid_bento']]
env=dict(os.environ,MDB_AUDIT_COUNT='1');runs=[]
def run(row,version,repeat):
 exe=base/'baseline_probe' if version=='before' else base/'current/build/current_probe'
 proc=subprocess.run(['/usr/bin/time','-l',str(exe),'bella',row['path']],capture_output=True,env=env,timeout=60)
 assert proc.returncode==0,proc.stderr
 data=json.loads(proc.stdout);rss=re.search(r'(\d+)\s+maximum resident set size',proc.stderr.decode());assert rss
 assert data['reading']==1
 return {'sha256':row['sha256'],'path':row['path'],'version':version,'repeat':repeat,'parse_ms':data['parseNs']/1e6,'rss_bytes':int(rss[1]),'files':data['filesCount'],'masters':data['mastersCount']}
for row in inputs:
 for v in ['before','after']:run(row,v,-1)
for repeat in range(9):
 for row in inputs:
  for v in (['before','after'] if repeat%2==0 else ['after','before']):runs.append(run(row,v,repeat))
summary=[]
for row in inputs:
 modes={v:{'parse_ms_median':statistics.median(x['parse_ms'] for x in runs if x['sha256']==row['sha256'] and x['version']==v),'rss_bytes_median':statistics.median(x['rss_bytes'] for x in runs if x['sha256']==row['sha256'] and x['version']==v)} for v in ['before','after']}
 summary.append({'path':row['path'],'sha256':row['sha256'],**modes,'time_change_percent':100*(modes['after']['parse_ms_median']/modes['before']['parse_ms_median']-1),'rss_change_bytes':modes['after']['rss_bytes_median']-modes['before']['rss_bytes_median']})
result={'method':'Same Release harness before and after JPED-only fix; seven original Avid MDBs, one warmup then nine alternating-order isolated process repetitions, count-only output; peak process RSS. No heavy comparison jobs intentionally running concurrently.','runs':runs,'summary':summary,'sum_median_ms':{v:sum(r[v]['parse_ms_median'] for r in summary) for v in ['before','after']}}
(base/'benchmark-delta.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='runs'},indent=2))
