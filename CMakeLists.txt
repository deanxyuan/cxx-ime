cmake_minimum_required(VERSION 3.15)
project(cxxime VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
    # Static link VC++ runtime — no vcredist dependency on target machines
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
endif()

option(CXXIME_BUILD_TESTS "Build tests" ON)

add_subdirectory(third_party/sqlite3)
add_subdirectory(third_party/nlohmann)
add_subdirectory(shared)
add_subdirectory(engine)
add_subdirectory(ipc)
add_subdirectory(tsf)
add_subdirectory(server)
add_subdirectory(ui)

if(CXXIME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
