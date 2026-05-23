project(cxxime-test CXX)

add_executable(cxxime-test
    test_main.cc
    engine_test.cc
    segmentor_test.cc
    dict_test.cc
    config_test.cc
)

target_link_libraries(cxxime-test PRIVATE
    cxxime-shared
    cxxime-engine
)

add_test(NAME cxxime-test COMMAND cxxime-test)
