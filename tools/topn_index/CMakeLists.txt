project(topn_index_tools CXX)

add_library(cxxime-topn-index-tools STATIC
    index_reader.cc
    index_writer.cc
    legacy_reader.cc
)
target_include_directories(cxxime-topn-index-tools PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(cxxime-topn-index-tools PUBLIC darts_clone)

add_executable(topn_builder main.cc)
target_link_libraries(topn_builder PRIVATE cxxime-topn-index-tools)

add_executable(topn_benchmark benchmark.cc)
target_link_libraries(topn_benchmark PRIVATE cxxime-topn-index-tools)

if(CXXIME_BUILD_TESTS)
    add_executable(topn_index_test topn_index_test.cc)
    target_link_libraries(topn_index_test PRIVATE cxxime-topn-index-tools)
    add_test(NAME topn_index_test COMMAND topn_index_test)
endif()
