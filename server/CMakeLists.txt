project(cxxime-server CXX)

add_executable(cxxime-server WIN32
    src/main.cc
    src/server_app.cc
    src/session_manager.cc
)

target_include_directories(cxxime-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cxxime-server PRIVATE
    cxxime-shared
    cxxime-engine
    cxxime-ipc
    shell32
    advapi32
)
