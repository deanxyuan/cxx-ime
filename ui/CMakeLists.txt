project(cxxime-ui CXX)

add_library(cxxime-ui STATIC
    src/background_image.cc
    src/background_layout.cc
    src/background_loader.cc
    src/candidate_window.cc
    src/d2d_renderer.cc
    src/gdi_renderer.cc
    src/layout.cc
    src/theme.cc
    src/status_window.cc
    src/window_position.cc
)

target_include_directories(cxxime-ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(cxxime-ui PUBLIC
    cxxime-shared
    d2d1
    dwmapi
    dwrite
    windowscodecs
    bcrypt
    msimg32
    ole32
)
