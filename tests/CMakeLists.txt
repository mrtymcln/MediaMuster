### MediaMuster test suite ###

### Same exact Qt release as the app; the Check the version step holds the two together ###
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Test)

### Test target helper ###
function(mediamuster_add_test name)
    add_executable(${name}
        ${name}.cpp
        ${CMAKE_SOURCE_DIR}/src/logcategories.cpp
        ${ARGN}
    )
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    ### Two loggers: stdout for local runs, a file for CI — the Windows ###
    ### runner captures no test stdout at all, so the file is the only ###
    ### record the workflow can print after a failure ###
    add_test(NAME ${name} COMMAND ${name} -o -,txt -o ${CMAKE_CURRENT_BINARY_DIR}/${name}.result.txt,txt)
endfunction()

### Tier 1 ###

# Header-only namespaces.
mediamuster_add_test(tst_mobid)
mediamuster_add_test(tst_logfile ${CMAKE_SOURCE_DIR}/src/logfile.cpp)
mediamuster_add_test(tst_pathkey)
mediamuster_add_test(tst_pmrkey)
mediamuster_add_test(tst_format)

# Conventions: the agreed folder names, essence-name rules and folder
# limits every feature keys on (header-only).
mediamuster_add_test(tst_conventions)

# OmfUid: the OMF-era 8/12-byte -> 32-byte id wrap (header-only), pinned
# against the real version-2 PMRs in fixtures/omf.
mediamuster_add_test(tst_omfuid)
target_compile_definitions(tst_omfuid PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# MediaCsv: the export's header/field alignment and the display columns
# it shares with the table. Lifted out of mainwindow.cpp so it is
# reachable from a test at all.
mediamuster_add_test(tst_mediacsv ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp)

# parseFolderName and the folder-name rules. Rebalancer executes via
# the ops engine now, so its tests link the engine too.
mediamuster_add_test(tst_rebalancer_parse
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/opundo.cpp
    ${CMAKE_SOURCE_DIR}/src/oprescue.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/trashrouter.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)

# Golden tests for Rebalancer::computePlan over synthetic Avid
# folder trees, plus execute_* tests that now drive the engine-backed
# Rename machine end to end. Creates a few thousand empty files in
# QTemporaryDir for the cap-related tests — runs in well under a second.
mediamuster_add_test(tst_rebalancer_plan
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/opundo.cpp
    ${CMAKE_SOURCE_DIR}/src/oprescue.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/trashrouter.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)

# Engine v2 value types: the on-disk enum names (header-only).
mediamuster_add_test(tst_oprequest)

# Engine v2 identity: FileIdentity/VolumeIdentity plus the NativeFile
# platform primitives, over real temp files and fixture MXF headers.
mediamuster_add_test(tst_fileidentity
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
)
target_compile_definitions(tst_fileidentity PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Engine v2 journal: WAL writer + reader, retention, undo candidacy.
mediamuster_add_test(tst_opjournal
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
)

# Engine v2 runner: the state machines driven synchronously through a
# test sink — conflict rules, identity gates, trash tiers, journal order.
mediamuster_add_test(tst_oprunner
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/trashrouter.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)
target_compile_definitions(tst_oprunner PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Engine v2 facade: signals, worker thread, mid-flight behaviours
# (journal readable during a copy, cancel, mutation, stranded park).
mediamuster_add_test(tst_opmanager
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/opundo.cpp
    ${CMAKE_SOURCE_DIR}/src/oprescue.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/trashrouter.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)

# Engine v2 rescue: the launch sweep over hand-written journals — v1's
# decision tables plus identity guards, volume re-anchoring, undo runs.
mediamuster_add_test(tst_oprescue
    ${CMAKE_SOURCE_DIR}/src/oprescue.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
)
target_compile_definitions(tst_oprescue PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Edit-Undo: forward run through the real runner, then reverse it and
# check the disk. The whole single-level undo contract lives here.
mediamuster_add_test(tst_opundo
    ${CMAKE_SOURCE_DIR}/src/opundo.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/trashrouter.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/oprescue.cpp
    ${CMAKE_SOURCE_DIR}/src/fileidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)

# Collect macOS crash reports into the logs folder.
mediamuster_add_test(tst_crashcollector
    ${CMAKE_SOURCE_DIR}/src/crashcollector.cpp
)

# AvbParser: MOB extraction from a bin, driven by hand-crafted byte buffers,
# plus the two real OMF-era bins in fixtures/omf (raw wrapped MOBs).
mediamuster_add_test(tst_avbparser
    ${CMAKE_SOURCE_DIR}/src/avbparser.cpp
)
target_compile_definitions(tst_avbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# MxfParser: codec-UL lookup table + DNxHD bitrate/fps + family fallback,
# plus clip-name/rate/audio pinning against real Avid header slices in
# fixtures/avid_headers.
mediamuster_add_test(tst_mxfparser
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
)
target_compile_definitions(tst_mxfparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# BentoFile: the container primitive under msmMMOB.mdb — label, TOC,
# immediate/offset values, the file's own property dictionary — against
# builder-made containers (tests/testbento.h) and the real MDB fixtures.
mediamuster_add_test(tst_bentofile
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
)
target_compile_definitions(tst_bentofile PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# PmrParser: fixed-layout msmFMID.pmr, driven by hand-crafted byte buffers.
mediamuster_add_test(tst_pmrparser
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
)
target_compile_definitions(tst_pmrparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# MdbParser: msmMMOB.mdb read through its Bento table of contents. Links
# PmrParser and MxfParser too: the corpus databases are joined through their
# PMRs to the real MXF headers captured from the same folders, with each
# header as ground truth for every technical field.
mediamuster_add_test(tst_mdbparser
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfobjects.cpp
    ${CMAKE_SOURCE_DIR}/src/omfresolutions.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
)
target_compile_definitions(tst_mdbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# OmfParser: one OMF-era essence file read tail-first, pinned against all
# 80 shipped slates and the two MC 2026 audio files. Links PmrParser and
# MdbParser too: each file's facts are joined through the version-2 PMR
# to the msmMMOB.mdb row for the same file, which must agree.
mediamuster_add_test(tst_omfparser
    ${CMAKE_SOURCE_DIR}/src/omfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfobjects.cpp
    ${CMAKE_SOURCE_DIR}/src/omfresolutions.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
)
target_compile_definitions(tst_omfparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# AvidEffects: precompute clip name -> Avid effect name + palette
# category, from the catalogue compiled into avideffects.cpp itself.
mediamuster_add_test(tst_avideffects
    ${CMAKE_SOURCE_DIR}/src/avideffects.cpp
)

### Tier 2 — fixture-backed ###

mediamuster_add_test(tst_scanner
    ${CMAKE_SOURCE_DIR}/src/mediascanner.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfobjects.cpp
    ${CMAKE_SOURCE_DIR}/src/omfresolutions.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/avideffects.cpp
)
target_link_libraries(tst_scanner PRIVATE Qt6::Concurrent)
target_compile_definitions(tst_scanner PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

### Tier 3 — Qt model behaviour ###

# The model is QtCore only (QAbstractTableModel); no widgets, no icons.
mediamuster_add_test(tst_mediatablemodel
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)

# MediaFilterProxy: search across Unicode normalisation forms (NFC vs NFD).
mediamuster_add_test(tst_mediafilterproxy
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)

# Effect picker: choose names and a volume, then use the actual table filter.
mediamuster_add_test(tst_effectfilterdialog
    ${CMAKE_SOURCE_DIR}/src/effectfilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)
target_link_libraries(tst_effectfilterdialog PRIVATE Qt6::Widgets)
