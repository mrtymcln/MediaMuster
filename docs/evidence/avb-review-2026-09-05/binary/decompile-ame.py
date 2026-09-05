doc = Document.getCurrentDocument()
doc.waitForBackgroundProcessToEnd()
targets = [('Write_AAFMobID', 2316940), ('Read_AAFMobID', 2317296), ('Write_OMFMobID', 2316568), ('Read_OMFMobID', 2316724), ('ASourceClip_Put', 1955296), ('ASourceClip_Get', 1955768), ('AComposition_Put', 1575536), ('AComposition_Get', 1578016), ('AMCBinRef_Get',0x2d5b48), ('AMCBinRef_Put',0x2d5990)]
for name, address in targets:
    segment = doc.getSegmentAtAddress(address)
    if segment.getProcedureAtAddress(address) is None:
        segment.markAsProcedure(address)
doc.waitForBackgroundProcessToEnd()
for name, address in targets:
    try:
        procedure = doc.getSegmentAtAddress(address).getProcedureAtAddress(address)
        with open('/tmp/mediamuster-avb-review/binary/'+name+'.pseudo.txt','w') as f:
            f.write(name+' @ '+hex(address)+'\n')
            if procedure and procedure.getEntryPoint() == address:
                f.write('Verified procedure entry: '+hex(address)+'\n')
                f.write(str(procedure.decompile()))
            else:
                f.write('NO MATCHING PROCEDURE')
    except Exception as e:
        with open('/tmp/mediamuster-avb-review/binary/'+name+'.pseudo.txt','w') as f:
            f.write(repr(e))
with open('/tmp/mediamuster-avb-review/binary/ame-decompile-complete.txt','w') as f:
    f.write('done\n')
