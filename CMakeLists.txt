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
option(CXXIME_BUILD_TOOLS "Build development tools" ON)
option(CXXIME_BUILD_HOST_PROBE "Build the staged IME host diagnostic probe" OFF)
option(CXXIME_ENABLE_HOST_DIAGNOSTICS "Compile staged host diagnostic instrumentation" OFF)
set(CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR
    "${CMAKE_SOURCE_DIR}/diagnostics/host_takeover"
)

# Data directory resolution:
#   Production (default): CXXIME_PRODUCTION_BUILD=ON  → <exe_dir>/data/
#   Development:          CXXIME_PRODUCTION_BUILD=OFF → ${CMAKE_SOURCE_DIR}/data/
option(CXXIME_PRODUCTION_BUILD "Build for production install" ON)

if(NOT CXXIME_PRODUCTION_BUILD)
    add_compile_definitions(CXXIME_DATA_DIR="${CMAKE_SOURCE_DIR}/data/")
endif()

add_subdirectory(third_party/sqlite3)
add_subdirectory(third_party/nlohmann)
add_subdirectory(shared)
add_subdirectory(engine)
add_subdirectory(ipc)
add_subdirectory(legacy_ime)
add_subdirectory(resource)
add_subdirectory(tsf)
add_subdirectory(server)
add_subdirectory(ui)

if(CXXIME_BUILD_HOST_PROBE)
    add_subdirectory(diagnostics/host_takeover/probe)
endif()

add_custom_target(cxxime-platform-modules
    DEPENDS cxxime-ime cxxime-tsf
)
if(CXXIME_BUILD_HOST_PROBE)
    add_dependencies(cxxime-platform-modules cxxime-ime-host-probe)
endif()

if(CXXIME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(CXXIME_BUILD_TOOLS)
    add_subdirectory(tools/dict_query)
    add_subdirectory(tools/sqlite_query)
    add_subdirectory(tools/ipc_test)
    add_subdirectory(tools/candidate_window_tool)
    add_subdirectory(tools/tsf_position_tool)
    add_subdirectory(tools/status_window_tool)
    add_subdirectory(tools/query_bench)
    add_subdirectory(tools/tray_icon_tool)
    add_subdirectory(tools/punct_test)
endif()

add_subdirectory(settings)
