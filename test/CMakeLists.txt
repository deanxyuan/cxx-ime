project(cxxime-test CXX)

set(TEST_COMMON_SOURCES util/testutil.cc)
set(TEST_COMMON_DEPS cxxime-shared cxxime-engine cxxime-ui cxxime-ipc)
set(TEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/tsf/src)

add_library(cxxime-test-topn-data STATIC util/topn_test_data.cc)
target_link_libraries(cxxime-test-topn-data PUBLIC
    cxxime-engine
    cxxime-topn-index-builder
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
add_cxxime_test(dict_test)
add_cxxime_test(config_test)
add_cxxime_test(layout_test)
add_cxxime_test(preedit_mode_test)
add_cxxime_test(ipc_test)
add_cxxime_test(wubi_test)
add_cxxime_test(candidate_window_test)
add_cxxime_test(candidate_quality_test)
add_cxxime_test(status_window_test)
add_cxxime_test(benchmark_test)
add_cxxime_test(short_cache_test)
target_link_libraries(short_cache_test PRIVATE cxxime-test-topn-data)
add_cxxime_test(trace_test)
if(CXXIME_ENABLE_HOST_DIAGNOSTICS)
    add_cxxime_test(stage_trace_test)
endif()
add_cxxime_test(mpscq_test)
add_cxxime_test(control_channel_test)
add_cxxime_test(control_lifecycle_test)

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
add_cxxime_test(wubi_engine_test)
add_cxxime_test(processor_pagination_test)
add_cxxime_test(temporary_ascii_test)

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
add_test(NAME session_manager_status_test COMMAND session_manager_status_test)

add_executable(session_manager_integration_test
    session_manager_integration_test.cc
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/server/src/session_manager.cc
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
add_test(NAME session_manager_integration_test COMMAND session_manager_integration_test)

find_package(Python3 QUIET)
if(Python3_FOUND)
    add_test(
        NAME build_short_cache_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/build_short_cache_test.py
                --topn-builder $<TARGET_FILE:topn_builder>
    )
    find_program(POWERSHELL_EXECUTABLE powershell)
    if(POWERSHELL_EXECUTABLE)
        add_test(
            NAME stage_trace_tools_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stage_trace_tools_test.py
            --powershell ${POWERSHELL_EXECUTABLE}
        )
    endif()
endif()
