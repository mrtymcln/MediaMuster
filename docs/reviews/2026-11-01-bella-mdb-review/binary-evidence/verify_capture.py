"""Capture the active, loaded Hopper image through one bounded stdio session.

Use a single session: parallel Hopper XPC clients can return empty/error replies.
Open the intended arm64 image in Hopper first and wait for analysis to finish.
"""
import hashlib, json, pathlib, selectors, subprocess, sys, time

base = pathlib.Path(__file__).parent / "verification"
group = sys.argv[1]
document = sys.argv[2] if len(sys.argv) > 2 else group
groups = {
    'libameLibrary': [
        ('mc_descriptor','0x17c018'), ('mc_rate','0x257af0'),
        ('mc_project','0x282b9c'), ('mc_project_search','0x29e974'),
        ('mc_import_attr','0x2ab7c0'), ('mc_video_tracks','0x2197f0'),
        ('mc_bento_label','0xdda24'), ('mc_long','0xb7294'),
        ('mc_rational','0xb84e4'), ('mc_local_order','0xb2680'),
        ('mc_original_bin','0x282aa4'), ('mc_bin_record','0x2d5b48'),
    ],
    'AvidCore': [('mc_core_object','0x6e818'), ('mc_core_find','0x6cc7c'), ('mc_core_find_impl','0x6cd68')],
    'AvidMediaComposer': [('mc_main_category','0x100db5f64')],
    'MDVx': [
        ('mdvx_open','0x100061c10'), ('mdvx_toc01','0x100061fc0'),
        ('mdvx_toc02','0x100061e48'), ('mdvx_data','0x100061eac'),
        ('mdvx_uid','0x10006163c'), ('mdvx_uid_eq','0x1000616c8'),
        ('mdvx_enrich','0x100064794'), ('mdvx_project','0x100064dc4'),
        ('mdvx_project_attr','0x1000651dc'), ('mdvx_compression','0x100064b60'),
        ('mdvx_dictionary','0x1000625e4'), ('mdvx_string','0x100063a58'),
    ],
}
p = subprocess.Popen(['/Applications/Hopper Disassembler.app/Contents/MacOS/HopperMCPServer'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
serial = 0
def request(method, params):
    global serial
    serial += 1
    p.stdin.write(json.dumps({'jsonrpc':'2.0','id':serial,'method':method,'params':params}) + '\n'); p.stdin.flush()
    deadline = time.monotonic() + 50
    while time.monotonic() < deadline:
        sel=selectors.DefaultSelector(); sel.register(p.stdout, selectors.EVENT_READ)
        ready=sel.select(max(0,deadline-time.monotonic()));sel.close()
        if not ready:raise TimeoutError(method)
        line=p.stdout.readline()
        if not line:raise RuntimeError('Hopper MCP server closed output')
        obj=json.loads(line)
        if obj.get('id')==serial:return obj
    raise TimeoutError(method)
receipts=[]
try:
    request('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'MDBReadOnlyReview','version':'1'}})
    p.stdin.write(json.dumps({'jsonrpc':'2.0','method':'notifications/initialized'})+'\n');p.stdin.flush()
    current=request('tools/call',{'name':'current_document','arguments':{}})
    (base/(group+'-active-document.json')).write_text(json.dumps(current,indent=2)+'\n')
    print('current',current,flush=True)
    for label,address in groups[group]:
        for tool,suffix in [('procedure_info','info'),('procedure_assembly','assembly'),('procedure_pseudo_code','pseudo')]:
            started=time.monotonic()
            result=request('tools/call',{'name':tool,'arguments':{'document':document,'procedure':address}})
            content='\n'.join(x.get('text','') for x in result.get('result',{}).get('content',[]))
            if result.get('error') or result.get('result',{}).get('isError',False) or not content:
                receipts.append({'label':label,'image':group,'document':document,'address':address,'tool':tool,'error':result.get('error'),'isError':result.get('result',{}).get('isError',False),'content':content})
                (base/(group+'-capture-receipt.json')).write_text(json.dumps(receipts,indent=2)+'\n')
                print(label,suffix,'FAILED',result,flush=True)
                continue
            path=base/(label+'-'+suffix+'.json');path.write_text(json.dumps(result,indent=2)+'\n')
            (base/(label+'-'+suffix+'.txt')).write_text(content+'\n')
            receipts.append({'label':label,'image':group,'address':address,'tool':tool,'path':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'seconds':round(time.monotonic()-started,3),'error':result.get('error'),'isError':result.get('result',{}).get('isError',False)})
            (base/(group+'-capture-receipt.json')).write_text(json.dumps(receipts,indent=2)+'\n')
            print(label,suffix,len(content),result.get('error'),flush=True)
finally:
    p.terminate()
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:p.kill()
