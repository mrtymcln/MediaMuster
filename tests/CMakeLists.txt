### MediaMuster test suite ###

### Same exact Qt release as the app; the Check the version step holds the two together ###
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Test)

### Test target helper ###
function(mediamuster_add_test name label)
    add_executable(${name}
        ${name}.cpp
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

# Diagnostic messages and crash-report collection.
mediamuster_add_test(tst_diagnostics core
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)

# Header-only namespaces.
mediamuster_add_test(tst_mobid core)
mediamuster_add_test(tst_pathkey core)
mediamuster_add_test(tst_format core)
mediamuster_add_test(tst_mediaevidence core)
mediamuster_add_test(tst_metadataselectionpolicy core)
target_link_libraries(tst_metadataselectionpolicy PRIVATE mediamuster_mediaengine_core)

# Conventions: the agreed folder names, essence-name rules and folder
# limits every feature keys on (header-only).
mediamuster_add_test(tst_conventions core)

### Media ###

# Discovery and physical-file record model.
mediamuster_add_test(tst_mediaenginediscovery media)
target_link_libraries(tst_mediaenginediscovery PRIVATE mediamuster_mediaengine_core)

mediamuster_add_test(tst_mediaenginepmr media)
target_link_libraries(tst_mediaenginepmr PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaenginepmr PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Corrupt-input evidence retention, device failures and cancellation regressions.
mediamuster_add_test(tst_mediaenginepmrrobustness media)
target_link_libraries(tst_mediaenginepmrrobustness PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaenginepmrrobustness PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Fresh MDB graph reader: original bytes, dictionaries, references and real sources.
mediamuster_add_test(tst_mediaenginemdb media)
target_link_libraries(tst_mediaenginemdb PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaenginemdb PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Legacy OMF graphs and native audio metadata, with guarded sample ranges.
mediamuster_add_test(tst_mediaenginelegacy media)
target_link_libraries(tst_mediaenginelegacy PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaenginelegacy PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Build each reader/projector without the other family's implementation. Linking
# the full MediaEngine library here would hide an accidental cross-reader dependency.
set(MEDIAENGINE_ISOLATED_VALUE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/mediaengine/projection.cpp
    ${CMAKE_SOURCE_DIR}/src/mediaengine/compressionnames_p.cpp
    ${CMAKE_SOURCE_DIR}/src/mediaengine/metadataselectionpolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/effectcatalogue.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
foreach(family IN ITEMS MDB OMF)
    string(TOLOWER "${family}" family_lower)
    set(isolated_sources)
    foreach(source IN LISTS MEDIAENGINE_${family}_SOURCES)
        list(APPEND isolated_sources "${CMAKE_SOURCE_DIR}/${source}")
    endforeach()
    set(test_name "tst_mediaengine${family_lower}isolated")
    mediamuster_add_test(${test_name} media
        ${isolated_sources}
        ${MEDIAENGINE_ISOLATED_VALUE_SOURCES}
    )
    target_compile_definitions(${test_name} PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
endforeach()

# Full MXF metadata framing, scoped Primers, source graphs and guarded essence.
mediamuster_add_test(tst_mediaenginemxf media)
target_link_libraries(tst_mediaenginemxf PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaenginemxf PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Bin object graphs, source evidence and selected-sequence reference resolution.
mediamuster_add_test(tst_mediaengineavb media)
target_link_libraries(tst_mediaengineavb PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaengineavb PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
mediamuster_add_test(tst_mediaengineavbreferences media)
target_link_libraries(tst_mediaengineavbreferences PRIVATE mediamuster_mediaengine_core)

# MediaEngine matching, projections, header scheduling and presentation.
mediamuster_add_test(tst_mediaenginescan media
    ${CMAKE_SOURCE_DIR}/src/mediaengineadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/effectcatalogue.cpp
)
target_link_libraries(tst_mediaenginescan PRIVATE mediamuster_mediaengine)
target_compile_definitions(tst_mediaenginescan PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

mediamuster_add_test(tst_mediaenginedatabase media)
target_link_libraries(tst_mediaenginedatabase PRIVATE mediamuster_mediaengine)
target_compile_definitions(tst_mediaenginedatabase PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# MXF preparation: supported evidence owns its values after reading finishes.
mediamuster_add_test(tst_mediaenginemxfsource media)
target_link_libraries(tst_mediaenginemxfsource PRIVATE mediamuster_mediaengine)
target_compile_definitions(tst_mediaenginemxfsource PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Read-only scan profiler and supported-metadata/evidence comparison.
add_executable(mediaengine_compare mediaengine_compare.cpp
    ${CMAKE_SOURCE_DIR}/src/mediaengineadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp
)
set_target_properties(mediaengine_compare PROPERTIES AUTOMOC OFF)
target_include_directories(mediaengine_compare PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mediaengine_compare PRIVATE mediamuster_mediaengine Qt6::Core)
if(WIN32)
    target_link_libraries(mediaengine_compare PRIVATE Psapi)
endif()

# Shared compression names: recorded labels and qualified Avid operating points.
mediamuster_add_test(tst_mediaengine_compressionnames media)
target_link_libraries(tst_mediaengine_compressionnames PRIVATE mediamuster_mediaengine_core)

mediamuster_add_test(tst_mediaengine_omfprojection media)
target_link_libraries(tst_mediaengine_omfprojection PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaengine_omfprojection PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
mediamuster_add_test(tst_mediaengine_mxfprojection media)
target_link_libraries(tst_mediaengine_mxfprojection PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_mediaengine_mxfprojection PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# Real PMR/MDB/header joins across the 795 MXF and 82 legacy corpus files.
mediamuster_add_test(tst_readerregressions media)
target_link_libraries(tst_readerregressions PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_readerregressions PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

# OmfUid: the OMF-era 8/12-byte -> 32-byte id wrap (header-only), pinned
# against the real version-2 PMRs in fixtures/omf.
mediamuster_add_test(tst_omfuid media)
target_compile_definitions(tst_omfuid PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Recorded import-path basename handling.
mediamuster_add_test(tst_mediametadata media
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)

# AvbBinLoader: complete structured documents in both byte orders, malformed
# object/reference boundaries, typed identities and metadata ownership, plus
# the two real OMF-era bins in fixtures/omf.
mediamuster_add_test(tst_avbparser media
    ${CMAKE_SOURCE_DIR}/src/avbbinloader.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_link_libraries(tst_avbparser PRIVATE mediamuster_mediaengine_core)
target_compile_definitions(tst_avbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# AvidEffects: precompute clip name -> Avid effect name + palette
# category, from the catalogue compiled into effectcatalogue.cpp itself.
mediamuster_add_test(tst_avideffects media
    ${CMAKE_SOURCE_DIR}/src/effectcatalogue.cpp
)

mediamuster_add_test(tst_scanner media
    ${CMAKE_SOURCE_DIR}/src/mediaengineadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mediascanner.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/effectcatalogue.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_link_libraries(tst_scanner PRIVATE Qt6::Concurrent mediamuster_mediaengine)
target_compile_definitions(tst_scanner PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

### Operations ###

# Identity-bound cleanup of operation-owned partial files and empty folders.
mediamuster_add_test(tst_opcleanup operations
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
)

# Volume identity and native durability primitives over real temporary files.
mediamuster_add_test(tst_volumeidentity operations
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
)

# Journal state/claim contracts and serialized operation/policy names, without a runner.
mediamuster_add_test(tst_opjournal operations
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
)

# Rebalance folder naming and redistribution plans over synthetic Avid folders.
# Execution contracts live in tst_fileoperations, keeping this planner-only.
mediamuster_add_test(tst_rebalanceplanner operations
    ${CMAKE_SOURCE_DIR}/src/opscanreceipt.cpp
    ${CMAKE_SOURCE_DIR}/src/rebalanceplanner.cpp
)

# File-operation engine, coordinator and Rebalance adapter contracts, including
# failures, retries, cancellation, recovery and Undo.
mediamuster_add_test(tst_fileoperations operations
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/rebalanceplanner.cpp
    ${CMAKE_SOURCE_DIR}/src/opscanreceipt.cpp
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/operationplan.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/operationrecovery.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/mediametadata.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_compile_definitions(tst_fileoperations PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_link_libraries(tst_fileoperations PRIVATE mediamuster_mediaengine_core)
target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash.cpp)
if(APPLE)
    target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash_mac.mm)
endif()
mediamuster_link_native_operations(tst_fileoperations)

### UI and export ###

# MediaCsv: the export's header/field alignment and shared display columns.
mediamuster_add_test(tst_mediacsv ui ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp)

# Table rows, Qt notifications and bin metadata enrichment/conflicts.
# The model is QtCore only (QAbstractTableModel); no widgets, no icons.
mediamuster_add_test(tst_mediatablemodel ui
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/avbmetadataresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp
)

# MediaFilterProxy: text normalization, classifications and row-level AVB algebra.
mediamuster_add_test(tst_mediafilterproxy ui
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/avbmetadataresolver.cpp
)

# Precompute picker: choose branches and a volume, then use the table filter.
mediamuster_add_test(tst_precomputefilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/precomputefilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/avbmetadataresolver.cpp
)
target_link_libraries(tst_precomputefilterdialog PRIVATE Qt6::Widgets)

# Bin picker: asynchronous loading/status and ordered expressions through the
# actual proxy, including empty operands and removed pending reads.
mediamuster_add_test(tst_binfilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/avbfilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/avbbinloader.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/avbmetadataresolver.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_link_libraries(tst_binfilterdialog PRIVATE Qt6::Widgets Qt6::Concurrent mediamuster_mediaengine_core)
set_tests_properties(tst_binfilterdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Production UI and coordinator behavior, with no alternate operation model.
set(_operation_ui_sources ${SOURCES})
list(FILTER _operation_ui_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
list(FILTER _operation_ui_sources EXCLUDE REGEX "\\.(icns|rc)$")
list(TRANSFORM _operation_ui_sources PREPEND "${CMAKE_SOURCE_DIR}/")
mediamuster_add_test(tst_operationui ui ${_operation_ui_sources})
target_include_directories(tst_operationui PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(tst_operationui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent mediamuster_mediaengine)
mediamuster_link_native_operations(tst_operationui)
if(APPLE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(tst_operationui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_operationui PRIVATE secur32)
endif()
set_tests_properties(tst_operationui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Model transfer receipts use the same native volume identity as operations.
foreach(_model_test tst_mediatablemodel tst_mediafilterproxy tst_precomputefilterdialog tst_binfilterdialog)
    target_link_libraries(${_model_test} PRIVATE mediamuster_mediaengine_core)
    target_sources(${_model_test} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/mediaengineadapter.cpp
        ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
        ${CMAKE_SOURCE_DIR}/src/nativefile.cpp)
endforeach()
