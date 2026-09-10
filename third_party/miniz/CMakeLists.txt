project(miniz C)

add_library(miniz STATIC
    miniz.c
    miniz_tdef.c
    miniz_tinfl.c
    miniz_zip.c
)

include(GenerateExportHeader)
generate_export_header(miniz)

target_include_directories(miniz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(miniz PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_STATIC_DEFINE)

if(MSVC)
    target_compile_options(miniz PRIVATE /W3)
endif()
