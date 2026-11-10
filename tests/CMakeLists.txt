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
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
)

# Core value types and metadata selection.
mediamuster_add_test(tst_mob_id_text core)
mediamuster_add_test(tst_bella_mob_id core)
target_link_libraries(tst_bella_mob_id PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bella_timing core)
target_link_libraries(tst_bella_timing PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bella_media_labels core)
target_link_libraries(tst_bella_media_labels PRIVATE mediamuster_bella)
mediamuster_add_test(tst_bella_matching core)
target_link_libraries(tst_bella_matching PRIVATE mediamuster_bella)
mediamuster_add_test(tst_path_key core)
mediamuster_add_test(tst_format_util core)

# Shared folder names, essence-name rules and folder limits.
mediamuster_add_test(tst_conventions core)

### Media ###

mediamuster_add_test(tst_bella_pmr media)
target_link_libraries(tst_bella_pmr PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bella_mdb media)
target_link_libraries(tst_bella_mdb PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bella_mxf media)
target_link_libraries(tst_bella_mxf PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bella_omf media)
target_link_libraries(tst_bella_omf PRIVATE mediamuster_bella)

mediamuster_add_test(tst_bella_audio media)
target_link_libraries(tst_bella_audio PRIVATE mediamuster_bella)

# Discovery and physical-file record model.
mediamuster_add_test(tst_bella_scanner media)
target_link_libraries(tst_bella_scanner PRIVATE mediamuster_bella)

# Bin object decoding and Whole Bin reference resolution.
mediamuster_add_test(tst_avb_reader media)
target_link_libraries(tst_avb_reader PRIVATE mediamuster_binreader)
mediamuster_add_test(tst_avb_references media)
target_link_libraries(tst_avb_references PRIVATE mediamuster_binreader)

# Shared compression names: recorded labels and qualified Avid operating points.
mediamuster_add_test(tst_compression_names media)
target_link_libraries(tst_compression_names PRIVATE mediamuster_bella)

# OmfUid: literal OMF-era 8/12-byte -> 32-byte identity wrapping controls.
mediamuster_add_test(tst_omf_uid media)

# Bin loading: both byte orders, malformed boundaries and file identities
# using authored disposable documents.
mediamuster_add_test(tst_avb_bin_loader media
    ${CMAKE_SOURCE_DIR}/src/avb/avb_bin_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
)
target_link_libraries(tst_avb_bin_loader PRIVATE mediamuster_binreader)

# Precompute names mapped to effect names and categories in the compiled catalogue.
mediamuster_add_test(tst_effect_catalogue media)
target_link_libraries(tst_effect_catalogue PRIVATE mediamuster_bella)

### Operations ###

# The operation verification boundary retains receipt policy and physical checks.
mediamuster_add_test(tst_file_check operations
    ${CMAKE_SOURCE_DIR}/src/operations/file_check.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
)
target_link_libraries(tst_file_check PRIVATE mediamuster_bella)

# Identity-bound cleanup of operation-owned partial files and empty folders.
mediamuster_add_test(tst_operation_cleanup operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
)

# Volume identity and native durability primitives over real temporary files.
mediamuster_add_test(tst_volume_identity operations
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
)

# Journal state/claim contracts and serialized operation/policy names, without a runner.
mediamuster_add_test(tst_operation_journal operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_journal.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
)

# Rebalance folder naming and redistribution plans over synthetic Avid folders.
# Execution contracts live in tst_file_operations, keeping this planner-only.
mediamuster_add_test(tst_rebalance_planner operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_request.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/rebalance_planner.cpp
)

# File-operation engine, coordinator and Rebalance adapter contracts, including
# failures, retries, cancellation, recovery and Undo.
mediamuster_add_test(tst_file_operations operations
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/rebalancer.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/rebalance_planner.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_request.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/file_check.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_plan.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_copier.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_journal.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_recovery.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
)
target_link_libraries(tst_file_operations PRIVATE mediamuster_bella)
target_sources(tst_file_operations PRIVATE ${CMAKE_SOURCE_DIR}/src/operations/operation_trash.cpp)
if(APPLE)
    target_sources(tst_file_operations PRIVATE ${CMAKE_SOURCE_DIR}/src/operations/operation_trash_mac.mm)
endif()
mediamuster_link_native_operations(tst_file_operations)

### UI and export ###

# MediaCsv: the export's header/field alignment and shared display columns.
mediamuster_add_test(tst_media_csv ui ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp)
target_link_libraries(tst_media_csv PRIVATE mediamuster_bella)

# Prepared table rows, QtCore model notifications and transfer updates.
mediamuster_add_test(tst_media_table_model ui
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp
)

# MediaFilterProxy: text normalization, classifications and row-level AVB algebra.
mediamuster_add_test(tst_media_filter_proxy ui
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
)

# Precompute picker: choose branches and a volume, then use the table filter.
mediamuster_add_test(tst_precompute_filter_dialog ui
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/precompute_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
)
target_link_libraries(tst_precompute_filter_dialog PRIVATE Qt6::Widgets)

# Bin picker: asynchronous loading/status and ordered expressions through the
# actual proxy, including empty operands and removed pending reads.
mediamuster_add_test(tst_avb_filter_dialog ui
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/avb_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/avb/avb_bin_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
)
target_link_libraries(tst_avb_filter_dialog PRIVATE Qt6::Widgets Qt6::Concurrent mediamuster_binreader)
set_tests_properties(tst_avb_filter_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Shared app sources used by the operation and scan UI tests.
set(_operation_ui_sources ${SOURCES})
list(FILTER _operation_ui_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
list(FILTER _operation_ui_sources EXCLUDE REGEX "\\.(icns|rc)$")
list(TRANSFORM _operation_ui_sources PREPEND "${CMAKE_SOURCE_DIR}/")
mediamuster_add_test(tst_operation_ui ui ${_operation_ui_sources} ${_bella_app_sources})
target_include_directories(tst_operation_ui PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(tst_operation_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent mediamuster_bella mediamuster_binreader)
mediamuster_link_native_operations(tst_operation_ui)
if(APPLE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/storage/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(tst_operation_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_operation_ui PRIVATE secur32)
endif()
set_tests_properties(tst_operation_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_bella_preview_ui ui ${_operation_ui_sources} ${_bella_app_sources})
target_include_directories(tst_bella_preview_ui PRIVATE ${CMAKE_BINARY_DIR})
if(MEDIAMUSTER_BELLA_PREVIEW)
    target_compile_definitions(tst_bella_preview_ui PRIVATE MEDIAMUSTER_BELLA_PREVIEW)
endif()
target_link_libraries(tst_bella_preview_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent mediamuster_bella mediamuster_binreader)
mediamuster_link_native_operations(tst_bella_preview_ui)
if(APPLE)
    target_link_libraries(tst_bella_preview_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_bella_preview_ui PRIVATE secur32)
endif()
set_tests_properties(tst_bella_preview_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_bella_presentation ui
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
    ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
)
target_link_libraries(tst_bella_presentation PRIVATE mediamuster_bella)

# Model transfer receipts use the same native volume identity as operations.
foreach(_model_test tst_media_table_model tst_media_filter_proxy tst_precompute_filter_dialog tst_avb_filter_dialog)
    target_link_libraries(${_model_test} PRIVATE mediamuster_bella)
    target_sources(${_model_test} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/operations/operation_request.cpp
        ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
        ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp)
endforeach()
