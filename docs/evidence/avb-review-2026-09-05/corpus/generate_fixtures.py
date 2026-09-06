import sys, pathlib, json
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
from avb.mobid import MobID
OUT=pathlib.Path('/tmp/mediamuster-avb-review/corpus/fixtures')
OUT.mkdir(exist_ok=True)
MASTER='060a2b340101010501010f1013000000443322116655887799aabbccddeeff00'
UNRELATED='060a2b340101010501010f1013000000efbeaddeaddeefbe0123456789abcdef'
for name,byte_order,comment in [('binary_only_le','little',False),('binary_only_be','big',False),('unrelated_comment_le','little',True)]:
    with avb.open() as f:
        mob=f.create.Composition(mob_type='MasterMob')
        mob.name='Tagged binary master'
        mob.mob_id=MobID(bytes_le=bytes.fromhex(MASTER))
        f.content.add_mob(mob)
        if comment: mob.attributes['Comments']='Unrelated historical ID: '+UNRELATED
        f.write(str(OUT/(name+'.avb')),byte_order=byte_order)
(OUT/'header_only.avb').write_bytes(b'\x06\x00DomainDJBO')
manifest=dict(master_id_le=MASTER,unrelated_comment_id_le=UNRELATED,notes='Synthesized with the reference writer, not saved or validated by Media Composer; first three reopen fully with the reference reader.')
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(OUT)
