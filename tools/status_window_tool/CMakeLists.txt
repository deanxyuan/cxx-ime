project(status_window_tool CXX)

add_executable(status_window_tool main.cc)

target_include_directories(status_window_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/ui/include
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(status_window_tool PRIVATE
    cxxime-ui
    cxxime-engine
    cxxime-shared
)
