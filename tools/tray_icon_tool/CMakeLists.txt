project(tray_icon_tool CXX)

add_executable(tray_icon_tool main.cc)

target_include_directories(tray_icon_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include
)

target_link_libraries(tray_icon_tool PRIVATE
    cxxime-shared
)
