# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

function(cxxime_add_version_resource target file_description original_filename file_type)
    if(NOT WIN32)
        return()
    endif()

    if(CXXIME_VERSION MATCHES "-")
        set(CXXIME_PRERELEASE_FLAG "VS_FF_PRERELEASE")
    else()
        set(CXXIME_PRERELEASE_FLAG "0")
    endif()

    set(CXXIME_FILE_DESCRIPTION "${file_description}")
    set(CXXIME_ORIGINAL_FILENAME "${original_filename}")
    set(CXXIME_FILE_TYPE "${file_type}")
    set(version_resource_dir "${CMAKE_CURRENT_BINARY_DIR}/version")
    set(version_resource "${version_resource_dir}/${target}_version.rc")
    file(MAKE_DIRECTORY "${version_resource_dir}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/version.rc.in" "${version_resource}" @ONLY)
    target_sources(${target} PRIVATE "${version_resource}")
endfunction()
