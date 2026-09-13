import json, shlex, subprocess, os
from pathlib import Path
b=Path(os.environ.get('MM_REVIEW_BUILD', '/tmp/mediamuster-review-20260906/build'))
work=Path(os.environ.get('MM_REVIEW_WORK', '/tmp/mediamuster-review-20260906-preserved'))
work.mkdir(parents=True, exist_ok=True)
db=json.loads((b/'compile_commands.json').read_text())
entry=next(x for x in db if x['file'].endswith('/src/main.cpp'))
cmd=shlex.split(entry['command'])
flags=[]
i=1
while i<len(cmd):
    if cmd[i] in ['-o','-c']:
        i+=2
        continue
    flags.append(cmd[i]); i+=1
probe=Path(__file__).resolve().with_name('ui_probe.cpp')
objects=[str(p) for p in (b/'CMakeFiles/MediaMuster.dir').rglob('*.o') if p.name not in ['main.cpp.o','rebalancedialog.cpp.o']]
qt=os.environ.get('MM_REVIEW_QT', '/Users/martymclean/Qt/6.5.3/macos')+'/lib'
link=[cmd[0],*flags,'-fno-access-control',str(probe),*objects,'-o',str(work/'ui_probe'),'-F'+qt,'-Wl,-rpath,'+qt]
for framework in ['QtWidgets','QtGui','QtConcurrent','QtCore']: link += ['-framework',framework]
subprocess.run(link,check=True)
