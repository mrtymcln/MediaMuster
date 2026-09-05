import pathlib,json,hashlib,collections,sys
sys.dont_write_bytecode=True
sys.path.insert(0,'/Users/martymclean/Downloads/pyavb-main/src')
import avb
from avb.utils import AVBObjectRef
OUT=pathlib.Path('/tmp/mediamuster-avb-review/corpus')
all_results=json.loads((OUT/'comparison.json').read_text()); real=all_results[:42]
def population(rows):
    role_counts=collections.Counter()
    for r in rows:
        for owners in r['missed_ids'].values():
            for o in owners:
                if o['chunk']=='CMPO': role_counts[f"{o['mob_type']} / {o.get('descriptor_class','no descriptor')} / usage {o.get('usage_code')}"]+=1
    return dict(files=len(rows),objects=sum(r['objects'] for r in rows),
        non_nil_id_bin_pairs=sum(len(r['structured_ids']) for r in rows),
        missed_non_nil_id_bin_pairs=sum(len(r['missed_ids']) for r in rows),
        extra_text_ids_without_structural_owner=sum(len(r['scan_ids_without_structured_owner']) for r in rows),
        missed_CMPO_role_counts=dict(role_counts))
extra=[]
for r in real:
    hs=r['scan_ids_without_structured_owner']
    if not hs: continue
    with avb.open(r['path'],use_ext=False) as f:
        for h in hs:
            direct_parents=[]
            for idx in range(1,len(f.object_positions)):
                if f.read_chunk(idx).class_id!=b'ATTR':continue
                obj=f.read_object(idx)
                for key,value in collections.OrderedDict.items(obj):
                    if isinstance(value,AVBObjectRef) and value.index==h['object_index']:direct_parents.append(dict(object_index=idx,attribute=key))
            extra.append(dict(path=r['path'],**h,attribute_parents=direct_parents))
sources=[pathlib.Path('/Users/martymclean/Developer/MediaMuster')/p for p in ['src/avbparser.cpp','src/avbparser.h','src/mobid.h','src/omfuid.h','tests/tst_avbparser.cpp']]
sources += [pathlib.Path('/Users/martymclean/Downloads/pyavb-main/src/avb')/p for p in ['file.py','ioctx.py','mobid.py','core.py','bin.py','components.py','trackgroups.py','misc.py','essence.py','attributes.py']]
summary=dict(populations=dict(attached=population(real[:14]),repo_OMF=population(real[14:16]),local_Media_Composer=population(real[16:]),total=population(real)),
    counting='Counts are (ID,bin) pairs, not deduplicated IDs across bins and not media files. Exclude 32-byte zero and Avid OMF-wrapped 8-byte zero source sentinel. Two repo OMF fixture bins duplicate two local bins and are deliberately distinct provenance populations.',
    checks=dict(cpp_scan_matches_python_mirror_all=all(r['cpp_vs_python_scan_agree'] for r in real),
        chunk_confined_49_byte_decoder_equals_pyavb_non_nil_ids_all=all(not r['tagged_missing_from_structured'] and not r['structured_missing_from_tagged'] for r in real),
        cpp_returns_valid_all=all(r['cpp_valid'] for r in real),
        pyavb_header_or_bin_failures=sum('error' in r for r in real),
        pyavb_object_failures=[dict(path=r['path'],failures=r['object_failures']) for r in real if r['object_failures']]),
    pyavb_limitation='Five CDCI descriptor objects in one MC bin reject extension 0x11. They are skipped by index; all CMPO/SCLP/other ID owners parse, and tag decoder differential still agrees. No claim pyavb fully supports every modern object.',
    media_join=dict(source='/tmp/mediamuster-audit/usage-identity-export-after.json',source_rows=2493,
        bin_media_pairs=363,missed_by_current_scavenger=0,
        caveat='A prior export snapshot, not a fresh media scan. It joins structured CMPO file/master IDs against MXF header file/master and MDB file/inferred-master, accepting both endian forms. Missing raw structured IDs do not prove missed media rows; all 363 matched pairs also join IDs current scan already finds. Five extra history text IDs match no row in this snapshot.'),
    extra_text_id_provenance=extra,
    minimal_fixtures=[dict(path=r['path'],size=r['size'],sha256=r['sha256'],pyavb_error=r.get('error'),structured_ids=list(r['structured_ids']),cpp_valid=r['cpp_valid'],cpp_forms=r['cpp_forms'],scan_hits=r['scan_hits'],object_failures=r['object_failures']) for r in all_results[42:]],
    fixture_caveat='Three structured synthetic bins reopen with attached pyavb; not tested by opening/saving in Media Composer. header_only is intentionally invalid/truncated.',
    binary_decoder_scope='Prototype scans exact 49-byte tagged envelopes only inside indexed chunks. This is feasibility evidence, not a production parser. Production should read known CMPO/SCLP fields, resolve ABIN item refs, normalize typed integers by file endian, explicitly exclude nil, and use class bounds to skip unrelated unknown classes. Of 1622 non-nil ID/bin pairs, 1609 are owned by CMPO; remaining 13 are MCMR references. Every missed ID has a CMPO owner.',
    source_fingerprints=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sources],
    corpus_manifest=[dict(path=r['path'],sha256=r['sha256'],size=r['size'],writer=r['writer'],byte_order=r['byte_order']) for r in real])
(OUT/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(dict(populations=summary['populations'],checks=summary['checks'],extra_text_id_parent_attributes=[x['attribute_parents'] for x in extra]),indent=2))
