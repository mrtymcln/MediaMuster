from pathlib import Path
import subprocess,concurrent.futures
root=Path('/Users/martymclean/Developer/MediaMuster')
qt=Path('/Users/martymclean/Qt/6.5.3/macos/lib')
base=['clang++','-std=c++17','-fPIC','-fsyntax-only','-x','c++','-','-I'+str(root/'src'),'-I'+str(root/'build'),'-F'+str(qt)]
for f in ['QtCore','QtGui','QtWidgets','QtConcurrent']:
 base+=['-I'+str(qt/f'{f}.framework/Headers')]
def check(p):
 r=subprocess.run(base,input='#include "'+p.name+'"\n',capture_output=True,text=True)
 return p.name,r.returncode,r.stderr
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
 results=list(pool.map(check,sorted((root/'src').glob('*.h'))))
for name,code,err in results:
 if code: print(name,err)
print('headers=',len(results),'failed=',sum(bool(c) for _,c,_ in results))
