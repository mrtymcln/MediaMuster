doc = Document.getCurrentDocument()
doc.waitForBackgroundProcessToEnd()
targets = [('AObjDoc_InitDomainOld', 819088), ('AObjDoc_ReadCB', 830632), ('AObjDoc_CreateObject', 831384), ('AObjDoc_LoadObject', 831880), ('AObjDoc_WriteObject', 824476), ('AObjDoc_WriteCB', 824380), ('AStream_WriteBytes', 151096), ('AStream_WriteUChar', 152368), ('AStream_WriteUInt32', 153808), ('AStream_ReadBytes', 157796), ('AStream_ReadUChar', 158788)]
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
with open('/tmp/mediamuster-avb-review/binary/core-decompile-complete.txt','w') as f:
    f.write('done\n')
