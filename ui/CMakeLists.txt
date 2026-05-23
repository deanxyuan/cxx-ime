project(cxxime-ui CXX)

add_library(cxxime-ui STATIC
    src/candidate_window.cc
    src/d2d_renderer.cc
    src/layout.cc
    src/theme.cc
)

target_include_directories(cxxime-ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-ui PUBLIC
    cxxime-shared
    d2d1
    dwrite
    windowscodecs
)
