project(cxxime-settings CXX)

add_executable(cxxime-settings WIN32 main.cc editor_app.cc editor_app.h settings.rc)

set_source_files_properties(settings.rc PROPERTIES
    COMPILE_FLAGS "/i${CMAKE_SOURCE_DIR}"
)

target_include_directories(cxxime-settings PRIVATE
    ${CXXIME_GENERATED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann
)

target_link_libraries(cxxime-settings PRIVATE
    cxxime-engine
    cxxime-ipc
    cxxime-shared
    cxxime-ui
    comctl32.lib
)

cxxime_add_version_resource(
    cxxime-settings
    "CxxIME Settings"
    "cxxime-settings.exe"
    VFT_APP
)
