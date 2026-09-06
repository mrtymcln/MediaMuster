"""Read-only AVB metadata evidence. Requires the supplied reference reader."""
import collections
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
PYAVB = Path('/Users/martymclean/Downloads/pyavb-main')
ROOT = Path('/Users/martymclean/Developer/MediaMuster')
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(PYAVB / 'src'))
import avb

paths = sorted((PYAVB / 'tests/test_files').rglob('*.avb'))
paths += sorted((ROOT / 'tests/fixtures/omf/mc2026_audio/bins').glob('*.avb'))
counts = collections.Counter()
owners = []
for path in paths:
    with avb.open(str(path), use_ext=False) as f:
        for i in range(1, len(f.object_positions)):
            if f.read_chunk(i).class_id == b'ATTR':
                for key in f.read_object(i):
                    if any(s in key.upper() for s in ('BIN', 'MOB', 'NAME', 'UTF', 'PJ', 'PROJ')):
                        counts[key] += 1
        for mob in f.content.mobs:
            if mob.class_id != b'CMPO':
                continue
            original = mob.attributes.get('_ORG_BIN')
            if getattr(original, 'class_id', None) != b'MCBR':
                continue
            owners.append(dict(
                path=str(path), object_index=mob.instance_id,
                name=mob.name, mob_type=mob.mob_type, usage_code=mob.usage_code,
                mob_id=bytes(mob.mob_id.bytes_le).hex(),
                original_bin_object_index=original.instance_id,
                bin_ref={k: v for k, v in original.property_data.items()
                         if isinstance(v, (str, int))},
                current_bin=path.stem))
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'attribute-inventory.json').write_text(json.dumps(dict(key_counts=dict(counts)), indent=2))
(OUT / 'original-bin-owners.json').write_text(json.dumps(owners, indent=2))
print(json.dumps(dict(files=len(paths), original_bin_owners=len(owners), key_counts=dict(counts)), indent=2))
