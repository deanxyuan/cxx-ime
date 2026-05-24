project(ipc_tool CXX)

add_executable(ipc_tool main.cc)

target_link_libraries(ipc_tool PRIVATE cxxime-ipc cxxime-shared)
