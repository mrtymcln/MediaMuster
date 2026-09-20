import os

out = '/tmp/mediamuster-format-audit-2026-09-20/binary'
doc = Document.getCurrentDocument()
doc.waitForBackgroundProcessToEnd()
targets = [
    ('IsOMFIFile_stream', 0xddc3c),
    ('IsOMFIFile_locator', 0xe4fbc),
    ('IsMXFFile', 0x485fb4),
    ('ReadPmrRec', 0x4474e0),
    ('LoadPMR', 0x4443f0),
    ('Read_OMFMobID', 0x2359b4),
    ('GetAccessTypeForFile', 0x44bf30),
    ('ScanDirectoryToCache', 0x44c33c),
    ('ScanFileToCache', 0x44cea0),
    ('DetermineFileType', 0x377e70),
]
for name, address in targets:
    segment = doc.getSegmentAtAddress(address)
    if segment.getProcedureAtAddress(address) is None:
        segment.markAsProcedure(address)
doc.waitForBackgroundProcessToEnd()
for name, address in targets:
    with open(os.path.join(out, name + '.pseudo.txt'), 'w') as stream:
        stream.write(name + ' @ ' + hex(address) + '\n')
        try:
            procedure = doc.getSegmentAtAddress(address).getProcedureAtAddress(address)
            if procedure and procedure.getEntryPoint() == address:
                stream.write('Verified procedure entry: ' + hex(address) + '\n')
                stream.write(str(procedure.decompile()))
            else:
                stream.write('NO MATCHING PROCEDURE')
        except Exception as error:
            stream.write(repr(error))
with open(os.path.join(out, 'decompile-complete.txt'), 'w') as stream:
    stream.write('Finished all requested procedures\n')
