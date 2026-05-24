project(cxxime-ipc CXX)

add_library(cxxime-ipc STATIC
    src/ipc_server.cc
    src/ipc_client.cc
)

target_include_directories(cxxime-ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-ipc PUBLIC
    cxxime-shared
)
