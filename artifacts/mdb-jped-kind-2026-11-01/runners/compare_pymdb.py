import pathlib, gzip, json, collections
BASE=pathlib.Path('/tmp/mediamuster-mdb-jped-20261101')
def key(x):
    if isinstance(x,dict):x=x.get('hex','')
    return x[32:48] if len(x)==64 else x[8:24] if len(x)==24 else x
def prop(o,k):
    vs=o.get('properties',{}).get(k,{}).get('values',[])
    return vs[-1] if vs else None
def srcids(t):
    if not isinstance(t,dict):return []
    out=[]
    if t.get('class')=='SCLP':
        v=prop(t,'OMFI:SCLP:SourceID')
        if v:out.append(key(v))
    for r in t.get('references',[]):out.extend(srcids(r))
    return out
results=[]
for p in sorted((BASE/'online/pymdb-results').glob('*.json.gz')):
    sha=p.name.split('.')[0];f=json.load(gzip.open(p,'rt'));b=json.loads((BASE/'current/results'/f'{sha}.bella.json').read_text())
    live=[m for m in f['mobs'] if m['live']]
    fileclasses={'MPGI','PCMA','CDCI','RGBA','JPED','WAVD','AIFD','TIFD','MDFL','MPGA','SD2D'}
    fs=[m for m in live if m.get('descriptor') and m['descriptor']['class'] in fileclasses]
    ms=[m for m in live if m['descriptor'] is None]
    fi=collections.defaultdict(list);mi=collections.defaultdict(list)
    for m in fs:fi[key(m['mob_id'])].append(m)
    for m in ms:mi[key(m['mob_id'])].append(m)
    row={'path':f['path'],'sha256':sha,'live_file_mobs':len(fs),'bella_files':len(b['files']),'live_master_mobs':len(ms),'bella_masters':len(b['masters']),'mismatches':[],'checks':collections.Counter(),'usage_hist':dict(collections.Counter(m['usage_code'] for m in live))}
    def check(cat,a,expected,identity):
        row['checks'][cat]+=1
        if a!=expected:row['mismatches'].append({'field':cat,'id':identity,'bella':a,'independent':expected})
    for file in b['files']:
        v=fi[key(file['id'])]
        if len(v)!=1:
            row['mismatches'].append({'field':'file-membership','id':file['id'],'candidate_ids':[x['object_id'] for x in v]});continue
        m=v[0];d=m['descriptor'];ident=file['id']
        check('file-descriptor-class',file['descriptorClass'],d['class'],ident)
        check('file-length',file.get('length'),prop(d,'OMFI:MDFL:Length'),ident)
        sr=file.get('sampleRate');er=prop(d,'OMFI:MDFL:SampleRate')
        sr=(sr['n'],sr['d']) if sr else None;er=(er['numerator'],er['denominator']) if er else None
        check('file-sample-rate',sr,er,ident)
        expected=[]
        target=key(m['mob_id'])
        for master in ms:
            if any(target in srcids(t) for t in master['tracks'] if isinstance(t,dict)):expected.append(key(master['mob_id']))
        actual=[key(b['masters'][i]['id']) for i in file['masterIndices']]
        check('master-links',sorted(actual),sorted(set(expected)),ident)
    for master in b['masters']:
        v=mi[key(master['id'])]
        if len(v)!=1:
            row['mismatches'].append({'field':'master-membership','id':master['id'],'candidate_ids':[x['object_id'] for x in v]});continue
        m=v[0]
        check('master-name',master['clipName'],m['name'] or '',master['id'])
        check('master-type',master['type'],2 if m['usage_code']==1 else 1,master['id'])
        bins=m['bins'];check('master-original-bin',master['originalBin'],bins[-1] if isinstance(bins,list) and bins else '',master['id'])
    row['checks']=dict(row['checks']);row['mismatch_hist']=dict(collections.Counter(x['field'] for x in row['mismatches']));results.append(row)
    print(json.dumps({k:v for k,v in row.items() if k!='mismatches'}),flush=True)
(BASE/'online/pymdb-vs-bella.json').write_text(json.dumps(results,indent=2))
