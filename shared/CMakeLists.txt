project(cxxime-shared CXX)

add_library(cxxime-shared STATIC
    src/key_event.cc
    src/data_path.cc
    src/config_monitor.cc
    src/dictionary_manifest.cc
    src/dictionary_monitor.cc
    src/diagnostics_config.cc
    src/mpscq.cc
    src/query_trace_log.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/stage_trace.cc
)

target_include_directories(cxxime-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/include
)

target_link_libraries(cxxime-shared PUBLIC shell32 bcrypt)
target_link_libraries(cxxime-shared PUBLIC nlohmann_json)
