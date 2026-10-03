import json
import os
from pathlib import Path
import subprocess

roots = [
    (Path('/Volumes/EDIT/Avid MediaFiles/MXF'), False),
    (Path('/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF'), False),
    (Path('/Users/Shared/AvidMediaComposer/OMFI MediaFiles'), True),
]
paths = []
issues = []
for root, include_root in roots:
    try:
        leaves = [root] if include_root else []
        with os.scandir(root) as entries:
            leaves.extend(Path(entry.path) for entry in entries if entry.is_dir(follow_symlinks=False))
        for leaf in sorted(leaves):
            with os.scandir(leaf) as entries:
                paths.extend(str(Path(entry.path)) for entry in entries
                             if entry.is_file(follow_symlinks=False) and entry.name.lower().endswith('.mdb'))
    except OSError as error:
        issues.append({'root': str(root), 'error': str(error)})
manifest = {'method': 'Read-only immediate managed media leaves; OMFI root included; no recursive walk or symlink following.',
            'roots': [str(root) for root, _ in roots], 'issues': issues, 'paths': sorted(paths)}
Path('/tmp/canon-mdb-real-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
with open('/tmp/canon-mdb-real-probe.jsonl', 'w') as output:
    result = subprocess.run(['/tmp/canon-mdb-real-probe', *manifest['paths']], stdout=output, text=True)
print(json.dumps({'files': len(paths), 'issues': issues, 'probeExit': result.returncode}))
