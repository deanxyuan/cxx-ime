project(tsf_position_tool CXX)

add_executable(tsf_position_tool main.cc)

target_include_directories(tsf_position_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/ui/include
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(tsf_position_tool PRIVATE
    cxxime-ui
    cxxime-engine
    cxxime-shared
)
