from pathlib import Path
import re, hashlib, sys
sources = [Path('docs/reviews/2026-10-03-format-audit/evidence/mxf_baseline_data_model.h'),Path('docs/reviews/2026-10-03-format-audit/evidence/mxf_extensions_data_model.h'),Path('/tmp/mediamuster-mxf-reference/libMXF-main/mxf/mxf_avid_extensions_data_model.h')]
texts=[p.read_text() for p in sources]
clean=[re.sub(r'/\*.*?\*/','',s,flags=re.S) for s in texts]
sets=[];items=[];types=[];members=[]
for s in clean:
    sets+=re.findall(r'MXF_SET_DEFINITION\(\s*(\w+)\s*,\s*(\w+)\s*,\s*MXF_LABEL\(([^)]+)\)\s*\)\s*;',s)
    items+=re.findall(r'MXF_ITEM_DEFINITION\(\s*(\w+)\s*,\s*(\w+)\s*,\s*MXF_LABEL\(([^)]+)\)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)\s*;',s)
    for m in re.finditer(r'MXF_(BASIC|ARRAY|COMPOUND|INTERPRETED)_TYPE_DEF\(\s*(\w+)\s*,\s*"([^"]+)"\s*(?:,\s*([^)]*))?\)\s*;',s):
        kind,token,name,args=m.groups(); args=[x.strip() for x in args.split(',')] if args else []
        fields=[]
        if kind=='COMPOUND':
            after=s[m.end():]; body=re.split(r'MXF_(?:BASIC|ARRAY|COMPOUND|INTERPRETED)_TYPE_DEF|MXF_SET_DEFINITION',after)[0]
            fields=re.findall(r'MXF_COMPOUND_TYPE_MEMBER\(\s*"([^"]+)"\s*,\s*(\w+)\s*\)\s*;',body)
        types.append((kind,token,name,args,fields))

def key(raw):return ''.join(f'{int(x.strip(),0):02x}' for x in raw.split(','))
def tid(token):return token.removeprefix('MXF_').removesuffix('_TYPE').title().replace('_','')
license=texts[0][:texts[0].index('*/')+2].replace(' * Baseline S377M MXF data model definitions',' * MXF schema catalogue derived from BBC libMXF data model definitions').replace('Copyright (C) 2012,','Copyright (C) 2006, 2009, 2012,')
out=[license,'\n#pragma once\n\n','// Static source vocabulary, not a property whitelist or display selection policy.\n// Generated from the exact primary definitions listed below. Numeric local tags\n// are intentionally omitted: only each source Primer Pack establishes those.\n// Upstream: https://github.com/bbc/libMXF/tree/main/mxf\n']
for p in sources:out.append(f'// {p.name} SHA-256 {hashlib.sha256(p.read_bytes()).hexdigest()}\n')
out+=['\nnamespace Canon::Detail::MxfSchema\n{\n','\tenum class Type\n\t{\n']
for _,t,_,_,_ in types:out.append(f'\t\t{tid(t)},\n')
out+=['\t};\n\n\tenum class Shape\n\t{\n\t\tBasic,\n\t\tArray,\n\t\tCompound,\n\t\tInterpreted\n\t};\n','\n\tstruct Member\n\t{\n\t\tconst char *name;\n\t\tType type;\n\t};\n','\n\tstruct TypeDefinition\n\t{\n\t\tconst char *name;\n\t\tShape shape;\n\t\tType base;\n\t\tunsigned size;\n\t\tunsigned firstMember;\n\t\tunsigned memberCount;\n\t};\n','\n\tinline constexpr Member members[] = {\n']
for _,_,_,_,fields in types:
 for name,t in fields:
  if name == 'Year':
   out.append("\t\t// ST 377-1:2019 section 4.3 and libMXF mxf_get_timestamp use signed Int16;\n\t\t// this corrects the baseline schema's UInt16 declaration without changing bytes.\n")
   t = 'MXF_INT16_TYPE'
  out.append(f'\t\t{{"{name}", Type::{tid(t)}}},\n')
out+=['\t};\n\n\tinline constexpr TypeDefinition types[] = {\n'];mi=0
for kind,token,name,args,fields in types:
 base=tid(args[0]) if kind in ('ARRAY','INTERPRETED') else tid(token)
 size=args[-1] if args else '0'
 out.append(f'\t\t{{"{name}", Shape::{kind.title()}, Type::{base}, {size}, {mi}, {len(fields)}}},\n');mi+=len(fields)
out+=['\t};\n','\n\tstruct SetDefinition\n\t{\n\t\tconst char *parent;\n\t\tconst char *name;\n\t\tconst char *key;\n\t};\n','\n\tinline constexpr SetDefinition sets[] = {\n']
for parent,name,k in sets:out.append(f'\t\t{{"{parent}", "{name}", "{key(k)}"}},\n')
out+=['\t};\n','\n\tstruct ItemDefinition\n\t{\n\t\tconst char *owner;\n\t\tconst char *name;\n\t\tconst char *key;\n\t\tType type;\n\t};\n','\n\tinline constexpr ItemDefinition items[] = {\n']
for owner,name,k,tag,t,required in items:out.append(f'\t\t{{"{owner}", "{name}", "{key(k)}", Type::{tid(t)}}},\n')
out+=['\t};\n}\n']
Path(sys.argv[1] if len(sys.argv) > 1 else 'src/canon/mxfcatalogue_p.h').write_text(''.join(out))
print(f'{len(types)} types {len(sets)} sets {len(items)} items {mi} compound members')
from collections import defaultdict
ks=defaultdict(list)
for owner,name,k,tag,t,required in items:ks[key(k)].append((owner,name,t))
print('duplicate UL',[(k,v) for k,v in ks.items() if len(v)>1])
