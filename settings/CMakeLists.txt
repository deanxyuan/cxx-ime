project(cxxime-settings CXX)

add_executable(cxxime-settings WIN32
    main.cc
    editor_about_panel.cc
    editor_advanced_panel.cc
    editor_app.cc
    editor_app.h
    editor_app_internal.h
    editor_candidate_panel.cc
    editor_controls.cc
    editor_diagnostics_panel.cc
    editor_dictionary_panel.cc
    editor_input_panel.cc
    editor_shortcuts_panel.cc
    settings.manifest
    settings.rc
)

set_source_files_properties(settings.rc PROPERTIES
    COMPILE_FLAGS "/i${CMAKE_SOURCE_DIR}"
)

target_include_directories(cxxime-settings PRIVATE
    ${CXXIME_GENERATED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/resource
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
