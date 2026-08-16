project(query_bench CXX)

add_executable(query_bench main.cc)

target_link_libraries(query_bench PRIVATE cxxime-engine)

add_executable(lexicon_bench
    lexicon_bench_main.cc
    lexicon_benchmark.cc
    lexicon_benchmark.h
    ${CMAKE_SOURCE_DIR}/settings/pinyin_code_paths.cc
    ${CMAKE_SOURCE_DIR}/settings/pinyin_code_paths.h
    ${CMAKE_SOURCE_DIR}/settings/system_lexicon_inspector.cc
    ${CMAKE_SOURCE_DIR}/settings/system_lexicon_inspector.h
)

target_include_directories(lexicon_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/settings
)

target_link_libraries(lexicon_bench PRIVATE cxxime-engine)

if(CXXIME_BUILD_TESTS)
    add_test(NAME lexicon_bench_help COMMAND lexicon_bench --help)
    add_test(NAME lexicon_bench_rejects_negative_repeat
             COMMAND lexicon_bench --data . --repeat -1
    )
    set_tests_properties(lexicon_bench_rejects_negative_repeat PROPERTIES WILL_FAIL TRUE)
endif()
