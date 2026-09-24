import json
import os
from pathlib import Path
import subprocess
import sys
import time

binary = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path(__file__).resolve().parent
output.mkdir(parents=True, exist_ok=True)
results = []
for guarded in (False, True):
    name = 'guard-on' if guarded else 'guard-off'
    command = [str(binary)] + (['--guard'] if guarded else [])
    environment = os.environ.copy()
    environment['QT_QPA_PLATFORM'] = 'cocoa'
    start = time.monotonic()
    with (output / (name + '.log')).open('wb') as log:
        try:
            process = subprocess.run(command, env=environment, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=45)
            result = {'mode': name, 'exit_code': process.returncode,
                      'seconds': round(time.monotonic() - start, 2)}
        except subprocess.TimeoutExpired:
            result = {'mode': name, 'timeout_seconds': 45}
    results.append(result)
    print(json.dumps(result), flush=True)
(output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
