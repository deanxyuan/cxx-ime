project(cxxime-ime-host-probe CXX)

add_executable(cxxime-ime-host-probe WIN32
    main.cc
    probe_app.cc
    probe_candidate_click.cc
    probe_imm_candidates.cc
    probe_ui_elements.cc
    probe_visibility.cc
    probe_window.cc
    ui_element_sink.cc
    window_owner_probe.cc
)

if(NOT CXXIME_ENABLE_HOST_DIAGNOSTICS)
    target_sources(cxxime-ime-host-probe PRIVATE
        ${CXXIME_HOST_TAKEOVER_DIAGNOSTICS_DIR}/src/stage_trace.cc
    )
endif()

target_link_libraries(cxxime-ime-host-probe PRIVATE
    cxxime-shared
    ole32
    oleaut32
    uuid
    imm32
    user32
    gdi32
    dwmapi
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CXXIME_PROBE_OUTPUT_NAME "cxxime-ime-host-probe-x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(CXXIME_PROBE_OUTPUT_NAME "cxxime-ime-host-probe-x86")
else()
    message(FATAL_ERROR "Unsupported Probe target pointer size: ${CMAKE_SIZEOF_VOID_P}")
endif()

set_target_properties(cxxime-ime-host-probe PROPERTIES
    OUTPUT_NAME "${CXXIME_PROBE_OUTPUT_NAME}"
)
