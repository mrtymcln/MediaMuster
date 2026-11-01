import subprocess,json,sys,select,time,os
p=subprocess.Popen(['/Applications/Hopper Disassembler.app/Contents/MacOS/HopperMCPServer'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
def call(id,method,params):
 p.stdin.write(json.dumps({'jsonrpc':'2.0','id':id,'method':method,'params':params})+'\n');p.stdin.flush()
 while True:
  line=p.stdout.readline()
  if not line: raise RuntimeError(p.stderr.read())
  obj=json.loads(line)
  if obj.get('id')==id:return obj
call(1,'initialize',{'protocolVersion':'2025-03-26','capabilities':{},'clientInfo':{'name':'pmr-review','version':'1'}})
p.stdin.write(json.dumps({'jsonrpc':'2.0','method':'notifications/initialized'})+'\n');p.stdin.flush()
r=call(2,'tools/call',{'name':sys.argv[1],'arguments':json.loads(sys.argv[2]) if len(sys.argv)>2 else {}})
print(json.dumps(r))
p.terminate()
