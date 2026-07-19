project(cxxime-ime CXX)

add_library(cxxime-ime SHARED
    src/legacy_common.cpp
    src/legacy_ime.cpp
    src/legacy_session.cpp
    src/legacy_ui.cpp
    cxxime_ime.def
)

target_include_directories(cxxime-ime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cxxime-ime PRIVATE
    cxxime-shared
    cxxime-ipc
    shell32
    imm32
    user32
    advapi32
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CXXIME_IME_OUTPUT_NAME "cxxime_ime_x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CXXIME_IME_OUTPUT_NAME "cxxime_ime_x86")
else()
    message(FATAL_ERROR "Unsupported IME target pointer size: ${CMAKE_SIZEOF_VOID_P}")
endif()

set_target_properties(cxxime-ime PROPERTIES
    OUTPUT_NAME "${CXXIME_IME_OUTPUT_NAME}"
    SUFFIX ".ime"
)
