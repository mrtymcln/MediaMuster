"""Stream independent otool arm64 disassembly; retain only reviewed procedure ranges."""
import concurrent.futures, hashlib, json, pathlib, re, subprocess
base = pathlib.Path(__file__).parent
out = base / 'verification'
identities = json.loads((base / 'binary-identity.json').read_text())
manifest = json.loads((base / 'capture-manifest.json').read_text())
groups = {}
for proc in manifest['procedures']:
    group = proc['captures'][0]['image']
    groups.setdefault(group, []).append((proc['label'], int(proc['entrypoint'], 16), int(proc['length'])))
groups['MDVx'].append(('mdvx_string', 0x100063a58, 0x158))
def capture(item):
    group = item['label']
    targets = groups[group]
    blocks = {label: [] for label, _, _ in targets}
    p = subprocess.Popen(['otool','-arch','arm64','-tvV',item['path']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for line in p.stdout:
        m = re.match(r'^([0-9a-fA-F]{16})\s', line)
        if not m:
            continue
        addr = int(m[1], 16)
        for label, start, size in targets:
            if start <= addr < start + size:
                blocks[label].append(line)
    errors = p.stderr.read()
    code = p.wait()
    receipt = {'image':group, 'exit_code':code, 'stderr':errors, 'procedures':[]}
    for label, start, size in targets:
        path = out / (label + '-otool.s')
        path.write_text(''.join(blocks[label]))
        receipt['procedures'].append({'label':label,'start':hex(start),'size':size,'instruction_lines':len(blocks[label]),'expected_instructions':size//4,'path':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    return receipt
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    receipts = list(pool.map(capture, identities))
(out/'independent-disassembly.json').write_text(json.dumps(receipts,indent=2)+'\n')
print(json.dumps([{'image':r['image'],'exit_code':r['exit_code'],'procedures':len(r['procedures']),'range_instruction_counts_complete':all(x['instruction_lines']==x['expected_instructions'] for x in r['procedures'])} for r in receipts],indent=2))
