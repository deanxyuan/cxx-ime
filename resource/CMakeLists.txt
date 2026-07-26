project(cxxime-resources C)

add_library(cxxime-resources SHARED
    cxxime-resources.rc
)

set_source_files_properties(cxxime-resources.rc PROPERTIES
    COMPILE_FLAGS "/i${CMAKE_CURRENT_SOURCE_DIR}/.."
)

set_target_properties(cxxime-resources PROPERTIES
    LINKER_LANGUAGE C
    LINK_FLAGS "/NOENTRY"
    OUTPUT_NAME "cxxime-resources"
    SUFFIX ".dll"
)

cxxime_add_version_resource(
    cxxime-resources
    "CxxIME Resource Library"
    "cxxime-resources.dll"
    VFT_DLL
)
