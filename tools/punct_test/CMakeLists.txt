project(punct_test CXX)

add_executable(punct_test main.cc)

target_link_libraries(punct_test PRIVATE cxxime-engine cxxime-shared)
