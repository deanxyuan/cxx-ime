project(cxxime-shared CXX)

add_library(cxxime-shared STATIC
    src/key_event.cc
)

target_include_directories(cxxime-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cxxime-shared PUBLIC shell32)
