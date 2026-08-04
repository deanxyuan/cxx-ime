project(cxxime-server CXX)

add_executable(cxxime-server WIN32
    src/config_store.cc
    src/config_write_coordinator.cc
    src/main.cc
    src/server_app.cc
    src/session_manager.cc
    src/user_dict_control_handler.cc
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

cxxime_add_version_resource(
    cxxime-server
    "CxxIME Server"
    "cxxime-server.exe"
    VFT_APP
)
