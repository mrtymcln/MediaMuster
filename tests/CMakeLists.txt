### MediaMuster test suite ###

# Keep Qt Test pinned to the application's Qt release.
find_package(Qt6 6.5.3 EXACT REQUIRED COMPONENTS Test)

### Test target helper ###
function(mediamuster_add_test name label)
    add_executable(${name}
        ${name}.cpp
        ${ARGN}
    )
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Core)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    # Save results as well as printing them: Windows CI can lose QtTest stdout.
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

# Core value types and metadata selection.
mediamuster_add_test(tst_mobid core)
mediamuster_add_test(tst_bellamobid core)
target_link_libraries(tst_bellamobid PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bellatiming core)
target_link_libraries(tst_bellatiming PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bellanaming core)
target_link_libraries(tst_bellanaming PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bellamatching core)
target_link_libraries(tst_bellamatching PRIVATE mediamuster_bella)
mediamuster_add_test(tst_pathkey core)
mediamuster_add_test(tst_format core)

# Shared folder names, essence-name rules and folder limits.
mediamuster_add_test(tst_conventions core)

### Media ###

mediamuster_add_test(tst_bellapmr media)
target_link_libraries(tst_bellapmr PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bellamdb media)
target_link_libraries(tst_bellamdb PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bellamxf media)
target_link_libraries(tst_bellamxf PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bellaomf media)
target_link_libraries(tst_bellaomf PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bellaaudio media)
target_link_libraries(tst_bellaaudio PRIVATE mediamuster_bella)

# Discovery and physical-file record model.
mediamuster_add_test(tst_bellascanner media)
target_link_libraries(tst_bellascanner PRIVATE mediamuster_bella)

# Bin object decoding and Whole Bin reference resolution.
mediamuster_add_test(tst_mediaengineavb media)
target_link_libraries(tst_mediaengineavb PRIVATE mediamuster_binreader)
target_compile_definitions(tst_mediaengineavb PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
mediamuster_add_test(tst_mediaengineavbreferences media)
target_link_libraries(tst_mediaengineavbreferences PRIVATE mediamuster_binreader)

# Shared compression names: recorded labels and qualified Avid operating points.
mediamuster_add_test(tst_mediaengine_compressionnames media)
target_link_libraries(tst_mediaengine_compressionnames PRIVATE mediamuster_bella)

# OmfUid: the OMF-era 8/12-byte -> 32-byte id wrap (header-only), pinned
# against the real version-2 PMRs in fixtures/omf.
mediamuster_add_test(tst_omfuid media)
target_compile_definitions(tst_omfuid PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Bin loading: both byte orders, malformed boundaries and file identities,
# using authored documents and genuine OMF-era bins.
mediamuster_add_test(tst_avbparser media
    ${CMAKE_SOURCE_DIR}/src/avbbinloader.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_link_libraries(tst_avbparser PRIVATE mediamuster_binreader)
target_compile_definitions(tst_avbparser PRIVATE
    FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# Precompute names mapped to effect names and categories in the compiled catalogue.
mediamuster_add_test(tst_avideffects media
    ${CMAKE_SOURCE_DIR}/src/effectcatalogue.cpp
)

### Operations ###

# The operation verification boundary retains receipt policy and physical checks.
mediamuster_add_test(tst_filecheck operations
    ${CMAKE_SOURCE_DIR}/src/filecheck.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
)
target_link_libraries(tst_filecheck PRIVATE mediamuster_bella)

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
    ${CMAKE_SOURCE_DIR}/src/bellaoperationhandoff.cpp
    ${CMAKE_SOURCE_DIR}/src/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/rebalanceplanner.cpp
    ${CMAKE_SOURCE_DIR}/src/opscanreceipt.cpp
    ${CMAKE_SOURCE_DIR}/src/opmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/oprunner.cpp
    ${CMAKE_SOURCE_DIR}/src/filecheck.cpp
    ${CMAKE_SOURCE_DIR}/src/operationplan.cpp
    ${CMAKE_SOURCE_DIR}/src/opcopier.cpp
    ${CMAKE_SOURCE_DIR}/src/opjournal.cpp
    ${CMAKE_SOURCE_DIR}/src/operationrecovery.cpp
    ${CMAKE_SOURCE_DIR}/src/opfile.cpp
    ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
    ${CMAKE_SOURCE_DIR}/src/nativefile.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_compile_definitions(tst_fileoperations PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_link_libraries(tst_fileoperations PRIVATE mediamuster_bella)
target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash.cpp)
if(APPLE)
    target_sources(tst_fileoperations PRIVATE ${CMAKE_SOURCE_DIR}/src/optrash_mac.mm)
endif()
mediamuster_link_native_operations(tst_fileoperations)

### UI and export ###

# MediaCsv: the export's header/field alignment and shared display columns.
mediamuster_add_test(tst_mediacsv ui ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp)
target_link_libraries(tst_mediacsv PRIVATE mediamuster_bella)

# Prepared table rows, QtCore model notifications and transfer updates.
mediamuster_add_test(tst_mediatablemodel ui
    ${CMAKE_SOURCE_DIR}/src/bellarowpresentation.cpp
    ${CMAKE_SOURCE_DIR}/src/bellaoperationhandoff.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp
)

# MediaFilterProxy: text normalization, classifications and row-level AVB algebra.
mediamuster_add_test(tst_mediafilterproxy ui
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)

# Precompute picker: choose branches and a volume, then use the table filter.
mediamuster_add_test(tst_precomputefilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/precomputefilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)
target_link_libraries(tst_precomputefilterdialog PRIVATE Qt6::Widgets)

# Bin picker: asynchronous loading/status and ordered expressions through the
# actual proxy, including empty operands and removed pending reads.
mediamuster_add_test(tst_binfilterdialog ui
    ${CMAKE_SOURCE_DIR}/src/avbfilterdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/avbbinloader.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/diagnostics.cpp
)
target_link_libraries(tst_binfilterdialog PRIVATE Qt6::Widgets Qt6::Concurrent mediamuster_binreader)
set_tests_properties(tst_binfilterdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Shared app sources used by the operation and scan UI tests.
set(_operation_ui_sources ${SOURCES})
list(FILTER _operation_ui_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
list(FILTER _operation_ui_sources EXCLUDE REGEX "\\.(icns|rc)$")
list(TRANSFORM _operation_ui_sources PREPEND "${CMAKE_SOURCE_DIR}/")
mediamuster_add_test(tst_operationui ui ${_operation_ui_sources} ${_bella_app_sources})
target_include_directories(tst_operationui PRIVATE ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_operationui PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_link_libraries(tst_operationui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent mediamuster_bella mediamuster_binreader)
mediamuster_link_native_operations(tst_operationui)
if(APPLE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(tst_operationui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_operationui PRIVATE secur32)
endif()
set_tests_properties(tst_operationui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_bellapreviewui ui ${_operation_ui_sources} ${_bella_app_sources})
target_include_directories(tst_bellapreviewui PRIVATE ${CMAKE_BINARY_DIR})
if(MEDIAMUSTER_BELLA_PREVIEW)
    target_compile_definitions(tst_bellapreviewui PRIVATE MEDIAMUSTER_BELLA_PREVIEW)
endif()
target_link_libraries(tst_bellapreviewui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent mediamuster_bella mediamuster_binreader)
mediamuster_link_native_operations(tst_bellapreviewui)
if(APPLE)
    target_link_libraries(tst_bellapreviewui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_bellapreviewui PRIVATE secur32)
endif()
set_tests_properties(tst_bellapreviewui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_bellapresentation ui
    ${CMAKE_SOURCE_DIR}/src/bellaoperationhandoff.cpp
    ${CMAKE_SOURCE_DIR}/src/bellarowpresentation.cpp
    ${CMAKE_SOURCE_DIR}/src/mediacsv.cpp
    ${CMAKE_SOURCE_DIR}/src/mediafilterproxy.cpp
    ${CMAKE_SOURCE_DIR}/src/mediatablemodel.cpp
)
target_link_libraries(tst_bellapresentation PRIVATE mediamuster_bella)

# Model transfer receipts use the same native volume identity as operations.
foreach(_model_test tst_mediatablemodel tst_mediafilterproxy tst_precomputefilterdialog tst_binfilterdialog)
    target_link_libraries(${_model_test} PRIVATE mediamuster_bella)
    target_sources(${_model_test} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/opscanreceipt.cpp
        ${CMAKE_SOURCE_DIR}/src/volumeidentity.cpp
        ${CMAKE_SOURCE_DIR}/src/nativefile.cpp)
endforeach()
