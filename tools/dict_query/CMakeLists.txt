project(dict_query CXX)

add_executable(dict_query main.cc)

target_link_libraries(dict_query PRIVATE cxxime-engine)
