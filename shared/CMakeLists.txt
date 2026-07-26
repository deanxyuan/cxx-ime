project(cxxime-shared CXX)

if(CXXIME_ENABLE_HOST_DIAGNOSTICS)
    set(CXXIME_SHARED_DIAGNOSTIC_SOURCES
        ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/stage_trace.cc
    )
else()
    set(CXXIME_SHARED_DIAGNOSTIC_SOURCES
        ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/stubs/stage_trace.cc
    )
endif()

add_library(cxxime-shared STATIC
    src/key_event.cc
    src/data_path.cc
    src/config_monitor.cc
    src/dictionary_manifest.cc
    src/dictionary_monitor.cc
    src/diagnostics_config.cc
    src/mpscq.cc
    src/query_trace_log.cc
    ${CXXIME_SHARED_DIAGNOSTIC_SOURCES}
)

target_include_directories(cxxime-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/include
)

target_link_libraries(cxxime-shared PUBLIC shell32 bcrypt)
target_link_libraries(cxxime-shared PUBLIC nlohmann_json)
