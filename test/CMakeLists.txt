project(cxxime-test CXX)

add_executable(cxxime-test
    test_main.cc
    util/testutil.cc
    engine_test.cc
    segmentor_test.cc
    dict_test.cc
    config_test.cc
    layout_test.cc
    preedit_mode_test.cc
    ipc_test.cc
)

target_include_directories(cxxime-test PRIVATE
    ${CMAKE_SOURCE_DIR}/tsf/src
)

target_link_libraries(cxxime-test PRIVATE
    cxxime-shared
    cxxime-engine
    cxxime-ui
    cxxime-ipc
)

add_test(NAME cxxime-test COMMAND cxxime-test)
