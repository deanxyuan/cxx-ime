project(cxxime-tsf CXX)

add_library(cxxime-tsf SHARED
    src/dllmain.cpp
    src/globals.cpp
    src/class_factory.cpp
    src/text_service.cpp
    src/key_event_sink.cpp
    src/edit_session.cpp
    src/display_attribute.cpp
    src/register.cpp
    cxxime_tsf.def
)

target_include_directories(cxxime-tsf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cxxime-tsf PRIVATE
    cxxime-shared
    cxxime-ipc
    cxxime-ui
    cxxime-engine
    ole32
    oleaut32
    uuid
    advapi32
)

set_target_properties(cxxime-tsf PROPERTIES
    OUTPUT_NAME "cxxime_tsf"
    SUFFIX ".dll"
)
