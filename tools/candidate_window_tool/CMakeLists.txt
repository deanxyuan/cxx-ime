project(candidate_window_tool CXX)

add_executable(candidate_window_tool main.cc)

target_include_directories(candidate_window_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/ui/include
)

target_link_libraries(candidate_window_tool PRIVATE
    cxxime-ui
    cxxime-engine
    cxxime-shared
)
