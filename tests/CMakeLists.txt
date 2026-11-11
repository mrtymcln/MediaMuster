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

set(_scanner_source_dir "${_app_source_dir}/scanner")
set(_timing_sources
    "${_scanner_source_dir}/timing.cpp"
    "${_scanner_source_dir}/audio_format_decoder_p.cpp"
)
set(_compression_name_sources
    "${_app_source_dir}/scanner_lookup/compression_names.cpp"
    "${_app_source_dir}/scanner_lookup/dnx_names.cpp"
)
set(_media_label_sources
    "${_scanner_source_dir}/media_labels.cpp"
    "${_app_source_dir}/scanner_lookup/effect_catalogue.cpp"
    ${_compression_name_sources}
)
set(_file_verification_sources
    "${_scanner_source_dir}/mxf_parser.cpp"
    "${_scanner_source_dir}/omf_parser.cpp"
    "${_scanner_source_dir}/wave_aiff_parser_p.cpp"
    "${_scanner_source_dir}/audio_format_decoder_p.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
)

# Core

mediamuster_add_test(tst_diagnostics core
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
)

mediamuster_add_test(tst_mob_id_text core)
mediamuster_add_test(tst_bella_mob_id core "${_scanner_source_dir}/mob_id.cpp")
mediamuster_add_test(tst_bella_timing core ${_timing_sources} "${_scanner_source_dir}/mob_id.cpp")
mediamuster_add_test(tst_bella_media_labels core ${_media_label_sources} "${_scanner_source_dir}/mob_id.cpp")
mediamuster_add_test(tst_bella_matching core
    "${_scanner_source_dir}/matching.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
    ${_timing_sources}
    ${_media_label_sources}
)
mediamuster_add_test(tst_path_key core)
mediamuster_add_test(tst_format_util core)

mediamuster_add_test(tst_conventions core)

# Media

mediamuster_add_test(tst_bella_pmr media
    "${_scanner_source_dir}/pmr_parser.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
)

mediamuster_add_test(tst_bella_mdb media
    "${_scanner_source_dir}/mdb_parser.cpp"
    "${_scanner_source_dir}/audio_format_decoder_p.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
)

mediamuster_add_test(tst_bella_mxf media
    "${_scanner_source_dir}/mxf_parser.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
)

mediamuster_add_test(tst_bella_omf media
    "${_scanner_source_dir}/omf_parser.cpp"
    "${_scanner_source_dir}/wave_aiff_parser_p.cpp"
    "${_scanner_source_dir}/audio_format_decoder_p.cpp"
    "${_scanner_source_dir}/mob_id.cpp"
)

mediamuster_add_test(tst_bella_audio media "${_scanner_source_dir}/wave_aiff_parser_p.cpp")
mediamuster_add_test(tst_bella_scanner media ${_bella_sources})

mediamuster_add_test(tst_avb_reader media ${_bin_reader_sources})
mediamuster_add_test(tst_avb_references media ${_bin_reader_sources})

mediamuster_add_test(tst_compression_names media ${_compression_name_sources})

mediamuster_add_test(tst_omf_uid media)

mediamuster_add_test(tst_avb_bin_loader media
    ${CMAKE_SOURCE_DIR}/src/avb/avb_bin_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
    ${_bin_reader_sources}
)

mediamuster_add_test(tst_effect_catalogue media "${_app_source_dir}/scanner_lookup/effect_catalogue.cpp")

# Operations

mediamuster_add_test(tst_file_check operations
    ${CMAKE_SOURCE_DIR}/src/operations/file_check.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
    ${_file_verification_sources}
)

mediamuster_add_test(tst_operation_cleanup operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
)

mediamuster_add_test(tst_volume_identity operations
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
)

mediamuster_add_test(tst_operation_journal operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_journal.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/operation_file.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
)

# tst_file_operations exercises execution, so this target only needs the planner.
mediamuster_add_test(tst_rebalance_planner operations
    ${CMAKE_SOURCE_DIR}/src/operations/operation_request.cpp
    ${CMAKE_SOURCE_DIR}/src/operations/rebalance_planner.cpp
)

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
    "${_scanner_source_dir}/timing.cpp"
    ${_file_verification_sources}
)
target_sources(tst_file_operations PRIVATE ${CMAKE_SOURCE_DIR}/src/operations/operation_trash.cpp)
if(APPLE)
    target_sources(tst_file_operations PRIVATE ${CMAKE_SOURCE_DIR}/src/operations/operation_trash_mac.mm)
endif()
mediamuster_link_native_operations(tst_file_operations)

# UI and export

mediamuster_add_test(tst_media_csv ui
    ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp
    ${_timing_sources}
)

mediamuster_add_test(tst_media_table_model ui
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp
    "${_scanner_source_dir}/mob_id.cpp"
    ${_timing_sources}
)

mediamuster_add_test(tst_media_filter_proxy ui
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    ${_timing_sources}
)

mediamuster_add_test(tst_precompute_filter_dialog ui
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/precompute_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
)
target_link_libraries(tst_precompute_filter_dialog PRIVATE Qt6::Widgets)

mediamuster_add_test(tst_avb_filter_dialog ui
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/avb_filter_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/avb/avb_bin_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    ${CMAKE_SOURCE_DIR}/src/support/diagnostics.cpp
    ${_bin_reader_sources}
)
target_link_libraries(tst_avb_filter_dialog PRIVATE Qt6::Widgets Qt6::Concurrent)
set_tests_properties(tst_avb_filter_dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_operation_ui ui ${_app_sources})
target_include_directories(tst_operation_ui PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(tst_operation_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Concurrent)
mediamuster_link_native_operations(tst_operation_ui)
if(APPLE)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/storage/revealinfinder.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(tst_operation_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(tst_operation_ui PRIVATE secur32)
endif()
set_tests_properties(tst_operation_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

mediamuster_add_test(tst_bella_presentation ui
    ${CMAKE_SOURCE_DIR}/src/media/bella_media_mapper.cpp
    ${CMAKE_SOURCE_DIR}/src/media/media_csv.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_filter_proxy.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/media_table_model.cpp
    "${_scanner_source_dir}/mob_id.cpp"
    "${_app_source_dir}/storage/volume_identity.cpp"
    ${_timing_sources}
)

# Model transfer receipts use the same native volume identity as operations.
foreach(_model_test tst_media_table_model tst_media_filter_proxy tst_precompute_filter_dialog tst_avb_filter_dialog)
    target_sources(${_model_test} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/operations/operation_request.cpp
        ${CMAKE_SOURCE_DIR}/src/storage/volume_identity.cpp
        ${CMAKE_SOURCE_DIR}/src/storage/file_sync.cpp
        ${_media_label_sources}
    )
endforeach()
