### MediaMuster test suite ###

### Same exact Qt release as the app; the number lives in the top-level CMakeLists.txt ###
find_package(Qt6 ${MEDIAMUSTER_QT_VERSION} EXACT REQUIRED COMPONENTS Test)

### Test target helper ###
function(mediamuster_add_test name)
    add_executable(${name}
        ${name}.cpp
        ${CMAKE_SOURCE_DIR}/src/logging.cpp
        ${ARGN}
    )
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

### Tier 1 ###

# Header-only namespaces.
mediamuster_add_test(tst_mobid)
mediamuster_add_test(tst_applog ${CMAKE_SOURCE_DIR}/src/applog.cpp)
mediamuster_add_test(tst_pathkey)
mediamuster_add_test(tst_pmrkey)
mediamuster_add_test(tst_format)

# AvidLayout: the shared folder-name and essence-name facts the scanner,
# rebalancer, and copy-path builder all key on (header-only).
mediamuster_add_test(tst_avidlayout)

# MediaCsv: the export's header/field alignment and the display columns
# it shares with the table. Lifted out of mainwindow.cpp so it is
# reachable from a test at all.
mediamuster_add_test(tst_mediacsv ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp)

# parseFolderName and the folder-name rules.
mediamuster_add_test(tst_rebalancer_parse
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
)

# Golden tests for Rebalancer::computePlan over synthetic Avid
# folder trees. Creates a few thousand empty files in QTemporaryDir
# for the cap-related tests — runs in well under a second locally.
mediamuster_add_test(tst_rebalancer_plan
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
)

# mediamanager.cpp uses xxhash for the verify pass, and now OpJournal
# for the write-ahead log in doMove/doDelete.
mediamuster_add_test(tst_mediamanager_paths
    ${CMAKE_SOURCE_DIR}/src/mediamanager.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
)

# OpJournal: write-ahead log writer + recovery reader fully isolated.
mediamuster_add_test(tst_opjournal
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
)

# OpRecovery: the crash-rollback engine. Drives hand-written journals
# over real files in a QTemporaryDir and checks what gets put back.
mediamuster_add_test(tst_oprecovery
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/oprecovery.cpp
)

# Collect macOS crash reports into the logs folder.
mediamuster_add_test(tst_crashcollector
    ${CMAKE_SOURCE_DIR}/src/crashcollector.cpp
)

# AvbParser: Bento-bin MOB extraction, driven by hand-crafted byte buffers.
mediamuster_add_test(tst_avbparser
    ${CMAKE_SOURCE_DIR}/src/avbparser.cpp
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

# MdbParser: msmMMOB.mdb read through its Bento table of contents. Links
# PmrParser and MxfParser too: the corpus databases are joined through their
# PMRs to the real MXF headers captured from the same folders, with each
# header as ground truth for every technical field.
mediamuster_add_test(tst_mdbparser
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
)
target_compile_definitions(tst_mdbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

### Tier 2 — fixture-backed ###

mediamuster_add_test(tst_scanner
    ${CMAKE_SOURCE_DIR}/src/mediascanner.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
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