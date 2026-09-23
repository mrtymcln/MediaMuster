from pathlib import Path
import subprocess
root=Path('/Users/martymclean/Developer/MediaMuster')
qt=Path('/Users/martymclean/Qt/6.5.3/macos')
objs=[str(p) for p in (root/'build/CMakeFiles/MediaMuster.dir').rglob('*.o') if p.name!='main.cpp.o']
args=['clang++','-std=c++17','-fPIC','-I'+str(root/'src'),'-I'+str(root/'build'),'-F'+str(qt/'lib')]
for f in ['QtCore','QtGui','QtWidgets','QtConcurrent']:
 args+=['-I'+str(qt/'lib'/f'{f}.framework/Headers')]
args+=['/tmp/mediamuster-review-20260922/ui_probe.cpp',*objs,'-Wl,-rpath,'+str(qt/'lib')]
for f in ['QtCore','QtGui','QtWidgets','QtConcurrent','Foundation','AppKit','OpenGL','ImageIO','Metal','IOKit','DiskArbitration']:
 args+=['-framework',f]
args+=['-o','/tmp/mediamuster-review-20260922/ui_probe']
subprocess.run(args,check=True)
