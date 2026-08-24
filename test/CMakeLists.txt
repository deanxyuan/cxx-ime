project(cxxime-test CXX)

set(TEST_COMMON_SOURCES util/testutil.cc)
set(TEST_COMMON_DEPS cxxime-shared cxxime-engine cxxime-ui cxxime-ipc)
set(TEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/tsf/src)

add_library(cxxime-test-topn-data STATIC util/topn_test_data.cc)
target_link_libraries(cxxime-test-topn-data PUBLIC
    cxxime-engine
    cxxime-topn-index-builder
)

add_library(cxxime-test-wubi-index-data STATIC util/wubi_index_test_data.cc)
target_include_directories(cxxime-test-wubi-index-data PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
)

function(add_cxxime_test name)
    add_executable(${name} ${name}.cc ${TEST_COMMON_SOURCES})
    target_include_directories(${name} PRIVATE ${TEST_INCLUDE_DIRS})
    # Tests always use the project's data/ directory regardless of CXXIME_PRODUCTION_BUILD
    target_compile_definitions(${name} PRIVATE
        CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
        CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
    )
    target_link_libraries(${name} PRIVATE ${TEST_COMMON_DEPS})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_cxxime_test(engine_test)
add_cxxime_test(segmentor_test)
add_cxxime_test(syllabifier_completion_test)
add_cxxime_test(pinyin_composer_test)
add_cxxime_test(dict_test)
add_cxxime_test(user_data_separation_test)
add_cxxime_test(disabled_system_lexicon_test)
target_link_libraries(disabled_system_lexicon_test PRIVATE cxxime-test-topn-data)
add_cxxime_test(dictionary_format_test)
target_link_libraries(dictionary_format_test PRIVATE cxxime-test-topn-data)
add_cxxime_test(dictionary_manifest_test)
add_cxxime_test(config_test)
add_cxxime_test(keyboard_shortcut_test)
add_cxxime_test(layout_test)
add_cxxime_test(preedit_mode_test)
add_cxxime_test(preedit_edit_test)
add_cxxime_test(edit_target_test)
target_sources(edit_target_test PRIVATE ${CMAKE_SOURCE_DIR}/tsf/src/edit_target.cc)
target_link_libraries(edit_target_test PRIVATE ole32 oleaut32 user32)
add_cxxime_test(effective_edit_target_test)
target_sources(effective_edit_target_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tsf/src/effective_edit_target.cpp
)
add_cxxime_test(candidate_presentation_test)
target_sources(candidate_presentation_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tsf/src/candidate_presentation.cpp
)
add_cxxime_test(ipc_test)
add_cxxime_test(wubi_test)
add_cxxime_test(wubi_prefix_query_test)
target_link_libraries(wubi_prefix_query_test PRIVATE cxxime-test-wubi-index-data)
add_cxxime_test(candidate_window_test)
add_cxxime_test(candidate_quality_test)
add_cxxime_test(status_window_test)
add_cxxime_test(window_position_test)
add_cxxime_test(benchmark_test)
add_cxxime_test(short_cache_test)
target_link_libraries(short_cache_test PRIVATE cxxime-test-topn-data)
add_cxxime_test(trace_test)
add_cxxime_test(diagnostic_log_maintenance_test)
if(CXXIME_ENABLE_HOST_DIAGNOSTICS)
    add_cxxime_test(host_trace_test)
endif()
add_cxxime_test(mpscq_test)
add_cxxime_test(control_channel_test)
add_cxxime_test(control_lifecycle_test)
add_cxxime_test(ui_channel_test)
add_cxxime_test(ui_presentation_router_test)
target_sources(ui_presentation_router_test PRIVATE
    ${CMAKE_SOURCE_DIR}/server/src/ui_presentation_router.cc
)
target_include_directories(ui_presentation_router_test PRIVATE
    ${CMAKE_SOURCE_DIR}/server/src
)
add_cxxime_test(system_lifecycle_monitor_test)
target_sources(system_lifecycle_monitor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/server/src/system_lifecycle_monitor.cc
)
target_include_directories(system_lifecycle_monitor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/server/src
)
target_link_libraries(system_lifecycle_monitor_test PRIVATE
    wtsapi32
)

add_executable(config_write_coordinator_test
    config_write_coordinator_test.cc
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/server/src/config_store.cc
    ${CMAKE_SOURCE_DIR}/server/src/config_write_coordinator.cc
)
target_include_directories(config_write_coordinator_test PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/server/src
)
target_compile_definitions(config_write_coordinator_test PRIVATE
    CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
    CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
)
target_link_libraries(config_write_coordinator_test PRIVATE ${TEST_COMMON_DEPS})
add_test(NAME config_write_coordinator_test COMMAND config_write_coordinator_test)

add_cxxime_test(dictionary_monitor_test)
add_cxxime_test(output_composer_test)
add_cxxime_test(engine_source_test)
add_cxxime_test(symbol_input_test)
add_cxxime_test(symbol_table_test)
add_cxxime_test(system_lexicon_inspector_test)
target_sources(system_lexicon_inspector_test PRIVATE
    ${CMAKE_SOURCE_DIR}/settings/pinyin_code_paths.cc
    ${CMAKE_SOURCE_DIR}/settings/system_lexicon_inspector.cc
)
target_include_directories(system_lexicon_inspector_test PRIVATE
    ${CMAKE_SOURCE_DIR}/settings
)
add_cxxime_test(lexicon_panel_model_test)
target_sources(lexicon_panel_model_test PRIVATE
    ${CMAKE_SOURCE_DIR}/settings/lexicon_panel_model.cc
)
target_include_directories(lexicon_panel_model_test PRIVATE
    ${CMAKE_SOURCE_DIR}/settings
)
add_cxxime_test(wubi_engine_test)
target_link_libraries(wubi_engine_test PRIVATE cxxime-test-wubi-index-data)
add_cxxime_test(processor_pagination_test)
add_cxxime_test(temporary_ascii_test)
add_cxxime_test(tsf_factory_test)
target_link_libraries(tsf_factory_test PRIVATE ole32 uuid)

add_cxxime_test(installer_lock_test)
target_link_libraries(installer_lock_test PRIVATE cxxime-installer-lock)

add_executable(candidate_preference_save_worker_test
    candidate_preference_save_worker_test.cc
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/server/src/candidate_preference_save_worker.cc
)
target_include_directories(candidate_preference_save_worker_test PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/server/src
)
target_compile_definitions(candidate_preference_save_worker_test PRIVATE
    CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
    CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
)
target_link_libraries(candidate_preference_save_worker_test PRIVATE ${TEST_COMMON_DEPS})
add_test(NAME candidate_preference_save_worker_test COMMAND candidate_preference_save_worker_test)

add_executable(session_manager_status_test
    session_manager_status_test.cc
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/server/src/session_manager.cc
)
target_include_directories(session_manager_status_test PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/server/src
)
target_compile_definitions(session_manager_status_test PRIVATE
    CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
    CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
)
target_link_libraries(session_manager_status_test PRIVATE ${TEST_COMMON_DEPS})
target_link_libraries(session_manager_status_test PRIVATE cxxime-test-topn-data)
target_link_libraries(session_manager_status_test PRIVATE cxxime-test-wubi-index-data)
add_test(NAME session_manager_status_test COMMAND session_manager_status_test)

add_executable(session_manager_integration_test
    session_manager_integration_test.cc
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/server/src/session_manager.cc
    ${CMAKE_SOURCE_DIR}/server/src/lexicon_control_handler.cc
)
target_include_directories(session_manager_integration_test PRIVATE
    ${TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/server/src
)
target_compile_definitions(session_manager_integration_test PRIVATE
    CXXIME_PROJECT_DIR="${CMAKE_SOURCE_DIR}/"
    CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/"
)
target_link_libraries(session_manager_integration_test PRIVATE ${TEST_COMMON_DEPS})
target_link_libraries(session_manager_integration_test PRIVATE cxxime-test-topn-data)
target_link_libraries(session_manager_integration_test PRIVATE cxxime-test-wubi-index-data)
add_test(NAME session_manager_integration_test COMMAND session_manager_integration_test)

find_package(Python3 QUIET)
if(Python3_FOUND)
    add_test(
        NAME dictionary_format_verifier_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/dictionary_format_verifier_test.py
    )
    add_test(
        NAME reverse_index_pipeline_test
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/reverse_index_pipeline_test.py
    )
    add_test(
        NAME wubi_symbol_pipeline_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wubi_symbol_pipeline_test.py
    )
    add_test(
        NAME wubi_prefix_index_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wubi_prefix_index_test.py
    )
    add_test(
        NAME pinyin_topn_pipeline_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pinyin_topn_pipeline_test.py
                --topn-builder $<TARGET_FILE:topn_builder>
    )
    find_program(POWERSHELL_EXECUTABLE powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(
            NAME host_trace_tools_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/host_trace_tools_test.py
                    --powershell ${POWERSHELL_EXECUTABLE}
        )
    endif()
endif()
