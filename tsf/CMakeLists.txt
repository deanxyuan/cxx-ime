project(cxxime-tsf CXX)

add_library(cxxime-tsf SHARED
    src/dllmain.cpp
    src/globals.cpp
    src/class_factory.cpp
    src/text_service.cpp
    src/text_service_candidate.cpp
    src/text_service_composition.cpp
    src/text_service_activation.cpp
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
    src/host_compatibility/host_classification_compatibility.cc
    src/host_compatibility/host_classification_runtime.cc
    src/host_compatibility/host_ime_private_api.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_composition.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_activation.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_imm_form.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_imm_mode.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_stage.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_window.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_imm_candidate_snapshot.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_imm_state.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_message.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_callsite.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_classification.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_sdl_message_hook.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_sdl_runtime.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_classification_message.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_host_ime_private_api.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_ui_element_identity.cc
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/tsf_ui_element_observer.cc
    cxxime_tsf.def
)

target_include_directories(cxxime-tsf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/resource
    ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src
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
