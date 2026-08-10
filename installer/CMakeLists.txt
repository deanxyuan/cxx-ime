project(cxxime-installer-helper CXX)

add_library(cxxime-installer-lock STATIC
    src/installer_lock.cc
)

target_include_directories(cxxime-installer-lock PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-installer-lock PUBLIC Rstrtmgr.lib)

add_executable(cxxime-installer-helper
    installer.manifest
    src/main.cc
    src/installer_prompt.cc
)

target_link_libraries(cxxime-installer-helper PRIVATE
    cxxime-installer-lock
    comctl32.lib
)

cxxime_add_version_resource(
    cxxime-installer-helper
    "CxxIME Installer Helper"
    "cxxime-installer-helper.exe"
    VFT_APP
)
