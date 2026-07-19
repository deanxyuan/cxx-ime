project(cxxime-tsf CXX)

add_library(cxxime-tsf SHARED
    src/dllmain.cpp
    src/globals.cpp
    src/class_factory.cpp
    src/text_service.cpp
    src/key_event_sink.cpp
    src/edit_session.cpp
    src/imm_bridge.cpp
    src/candidate_ui_element.cpp
    src/reading_ui_element.cpp
    src/display_attribute.cpp
    src/register.cpp
    src/resource_loader.cc
    src/language_bar.cpp
    src/status_controller.cc
    cxxime_tsf.def
)

target_include_directories(cxxime-tsf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/resource
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
    imm32
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CXXIME_TSF_OUTPUT_NAME "cxxime_tsf_x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CXXIME_TSF_OUTPUT_NAME "cxxime_tsf_x86")
else()
    message(FATAL_ERROR "Unsupported TSF target pointer size: ${CMAKE_SIZEOF_VOID_P}")
endif()

set_target_properties(cxxime-tsf PROPERTIES
    OUTPUT_NAME "${CXXIME_TSF_OUTPUT_NAME}"
    SUFFIX ".dll"
)
