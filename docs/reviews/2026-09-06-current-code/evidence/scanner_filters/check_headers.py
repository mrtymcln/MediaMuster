import json,pathlib,shlex,subprocess,concurrent.futures,os
base=pathlib.Path(os.environ.get('MM_REVIEW_WORK', '/tmp/mediamuster-review-20260906-preserved'))/'scanner_headers'
base.mkdir(parents=True,exist_ok=True)
build=pathlib.Path(os.environ.get('MM_REVIEW_BUILD', '/tmp/mediamuster-review-20260906/build'))
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(e for e in entries if e['file'].endswith('/mainwindow.cpp'))
args=shlex.split(entry['command']); flags=[];i=0
while i<len(args):
    a=args[i]
    if a=='-o': i+=2;continue
    if a=='-c' or a==entry['file']: i+=1;continue
    flags.append(a);i+=1
root=pathlib.Path('/Users/martymclean/Developer/MediaMuster')
headers=sorted((root/'src').rglob('*.h'))
folder=base/'header_checks';folder.mkdir(exist_ok=True)
def check(h):
    name=str(h.relative_to(root)).replace('/','_')
    unit=folder/(name+'.cpp');unit.write_text('#include "'+str(h)+'"\n')
    p=subprocess.run(flags+['-fsyntax-only',str(unit)],cwd=entry['directory'],capture_output=True,text=True)
    (folder/(name+'.log')).write_text(p.stdout+p.stderr)
    return str(h.relative_to(root)),p.returncode,p.stdout+p.stderr
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    results=list(pool.map(check,headers))
report=[]
for name,code,out in results:
    report.append(f'{name}: '+('PASS' if code==0 else 'FAIL'))
    if code: report.append(out)
report.append(f'TOTAL {len(results)} headers, {sum(x[1]!=0 for x in results)} failures')
text='\n'.join(report)+'\n';(base/'header_selfcontain.log').write_text(text);print(text)
