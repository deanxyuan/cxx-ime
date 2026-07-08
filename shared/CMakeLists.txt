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
)

target_include_directories(cxxime-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-shared PUBLIC shell32 bcrypt)
target_link_libraries(cxxime-shared PUBLIC nlohmann_json)
