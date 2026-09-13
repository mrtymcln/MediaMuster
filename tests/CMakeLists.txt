### MediaMuster test suite ###

### Same exact Qt release as the app; the Check the version step holds the two together ###
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Test)

### Test target helper ###
function(mediamuster_add_test name label)
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
    set_tests_properties(${name} PROPERTIES LABELS "${label}")
endfunction()

# Suites are registered in logical order. Labels allow focused CTest runs;
# they do not create dependencies between suites.

### Core ###

# Header-only namespaces.
mediamuster_add_test(tst_mobid core)
mediamuster_add_test(tst_logfile core ${CMAKE_SOURCE_DIR}/src/logfile.cpp)
mediamuster_add_test(tst_pathkey core)
mediamuster_add_test(tst_pmrkey core)
mediamuster_add_test(tst_format core)

# Conventions: the agreed folder names, essence-name rules and folder
# limits every feature keys on (header-only).
mediamuster_add_test(tst_conventions core)

# Collect macOS crash reports into the logs folder.
mediamuster_add_test(tst_crashcollector core
    ${CMAKE_SOURCE_DIR}/src/crashcollector.cpp
)

### Media ###

# OmfUid: the OMF-era 8/12-byte -> 32-byte id wrap (header-only), pinned
# against the real version-2 PMRs in fixtures/omf.
mediamuster_add_test(tst_omfuid media)
target_compile_definitions(tst_omfuid PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Shared codec lookup and metadata derivation, without binary parser dependencies.
mediamuster_add_test(tst_mediametadata media
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
)

# BentoFile: the container primitive under msmMMOB.mdb — label, TOC,
# immediate/offset values, the file's own property dictionary — against
# builder-made containers (tests/testbento.h) and the real MDB fixtures.
mediamuster_add_test(tst_bentofile media
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
)
target_compile_definitions(tst_bentofile PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# PmrParser: fixed-layout msmFMID.pmr, driven by hand-crafted byte buffers.
mediamuster_add_test(tst_pmrparser media
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
)
target_compile_definitions(tst_pmrparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# MdbParser: msmMMOB.mdb read through its Bento table of contents. Links
# PmrParser and MxfParser too: the corpus databases are joined through their
# PMRs to the real MXF headers captured from the same folders, with each
# header as ground truth for every technical field.
mediamuster_add_test(tst_mdbparser media
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfobjects.cpp
    ${CMAKE_SOURCE_DIR}/src/omfresolutions.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
)
target_compile_definitions(tst_mdbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# OmfParser: one OMF-era essence file read tail-first, pinned against all
# 80 shipped slates and the two MC 2026 audio files. Links PmrParser and
# MdbParser too: each file's facts are joined through the version-2 PMR
# to the msmMMOB.mdb row for the same file, which must agree.
mediamuster_add_test(tst_omfparser media
    ${CMAKE_SOURCE_DIR}/src/omfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/omfobjects.cpp
    ${CMAKE_SOURCE_DIR}/src/omfresolutions.cpp
    ${CMAKE_SOURCE_DIR}/src/bentofile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/pmrparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mdbparser.cpp
)
target_compile_definitions(tst_omfparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# AvbParser: complete structured documents in both byte orders, malformed
# object/reference boundaries, typed identities and metadata ownership, plus
# the two real OMF-era bins in fixtures/omf.
mediamuster_add_test(tst_avbparser media
    ${CMAKE_SOURCE_DIR}/src/avbparser.cpp
)
target_compile_definitions(tst_avbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# MxfParser: binary framing, metadata ownership and real Avid header fixtures,
# including the shared codec/rate/audio facts derived from decoded fields.
mediamuster_add_test(tst_mxfparser media
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
)
target_compile_definitions(tst_mxfparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# AvidEffects: precompute clip name -> Avid effect name + palette
# category, from the catalogue compiled into avideffects.cpp itself.
mediamuster_add_test(tst_avideffects media
    ${CMAKE_SOURCE_DIR}/src/avideffects.cpp
)

mediamuster_add_test(tst_scanner media
    ${CMAKE_SOURCE_DIR}/src/mediascanner.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
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

### Operations ###

# Operation request value types and their serialized enum names (header-only).
mediamuster_add_test(tst_oprequest operations)

# Volume identity plus the NativeFile
# platform primitives, over real temp files and fixture MXF headers.
mediamuster_add_test(tst_volumeidentity operations
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
)
target_compile_definitions(tst_volumeidentity PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Journal state/claim contracts use only filesystem primitives, without a runner.
mediamuster_add_test(tst_opjournal operations
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
)
mediamuster_link_native_operations(tst_opjournal)

# Rebalance folder naming and redistribution plans over synthetic Avid folders.
# Execution contracts live in tst_fileoperations, keeping this planner-only.
mediamuster_add_test(tst_rebalanceplanner operations
    ${CMAKE_SOURCE_DIR}/src/rebalanceplanner.cpp
)

# File-operation engine, coordinator and Rebalance adapter contracts, including
# failures, retries, cancellation, recovery and Undo.
mediamuster_add_test(tst_fileoperations operations
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/rebalanceplanner.cpp
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/opdiagnostics.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/operationplan.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/operationrecovery.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mxfparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/third_party/xxhash.c
)
target_compile_definitions(tst_fileoperations PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash.cpp)
if(APPLE)
    target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash_mac.mm)
endif()
mediamuster_link_native_operations(tst_fileoperations)
set_property(TARGET tst_fileoperations APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/tests/fixtures/file_operations/relatives.zip")
if(APPLE)
    # Exercise the same Contents/MacOS -> Contents/Resources lookup as the app.
    # A plain test executable missed the unclean ../Resources path regression.
    set_target_properties(tst_fileoperations PROPERTIES MACOSX_BUNDLE TRUE)
    set_source_files_properties(${MEDIAMUSTER_DIAGNOSTIC_SAMPLES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/file-operation-samples")
    target_sources(tst_fileoperations PRIVATE ${MEDIAMUSTER_DIAGNOSTIC_SAMPLES})
else()
    add_custom_command(TARGET tst_fileoperations POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_mm_sample_dir}"
            "$<TARGET_FILE_DIR:tst_fileoperations>/file-operation-samples"
        VERBATIM)
endif()

### UI and export ###

# MediaCsv: the export's header/field alignment and shared display columns.
mediamuster_add_test(tst_mediacsv ui ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp)

# Table rows, Qt notifications and bin metadata enrichment/conflicts.
# The model is QtCore only (QAbstractTableModel); no widgets, no icons.
mediamuster_add_test(tst_mediatablemodel ui
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/binmetadataresolver.cpp
)

# MediaFilterProxy: text normalization, classifications and row-level AVB algebra.
mediamuster_add_test(tst_mediafilterproxy ui
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/binmetadataresolver.cpp
)

# Effect picker: choose names and a volume, then use the actual table filter.
mediamuster_add_test(tst_effectfilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/effectfilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/binmetadataresolver.cpp
)
target_link_libraries(tst_effectfilterdialog PRIVATE Qt6::Widgets)

# Bin picker: asynchronous loading/status and ordered expressions through the
# actual proxy, including empty operands and removed pending reads.
mediamuster_add_test(tst_binfilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/binfilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/avbparser.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/binmetadataresolver.cpp
)
target_link_libraries(tst_binfilterdialog PRIVATE Qt6::Widgets Qt6::Concurrent)
set_tests_properties(tst_binfilterdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Production UI and coordinator behavior, with no alternate operation model.
set(_operation_ui_sources ${SOURCES})
list(FILTER _operation_ui_sources EXCLUDE REGEX "(^|/)(main|logcategories)\\.cpp$")
list(FILTER _operation_ui_sources EXCLUDE REGEX "\\.(icns|rc)$")
list(TRANSFORM _operation_ui_sources PREPEND "${CMAKE_SOURCE_DIR}/")
mediamuster_add_test(tst_operationui ui ${_operation_ui_sources})
target_include_directories(tst_operationui PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(tst_operationui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent)
mediamuster_link_native_operations(tst_operationui)
if(WIN32)
    target_link_libraries(tst_operationui PRIVATE secur32)
endif()
set_tests_properties(tst_operationui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
