project(cxxime-settings CXX)

add_executable(cxxime-settings WIN32 main.cc editor_app.cc editor_app.h)

target_include_directories(cxxime-settings PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(cxxime-settings PRIVATE
    cxxime-engine
    cxxime-shared
    comctl32.lib
)
