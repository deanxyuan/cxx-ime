// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_ACTIVATION_H_
#define CXXIME_HOST_TAKEOVER_TSF_ACTIVATION_H_

#include "pch.h"
#include "host_compatibility/host_classification_compatibility.h"

namespace cxxime_tsf {

void start_host_trace_runtime(const HostClassificationCompatibilitySnapshot& snapshot);
void stop_host_trace_runtime(const HostClassificationCompatibilitySnapshot& snapshot);

void trace_activation_step(const char* step,
                           const char* phase,
                           HRESULT result,
                           bool required);

void trace_thread_sinks(const char* action,
                        HRESULT source_result,
                        bool thread_focus_attempted,
                        HRESULT thread_focus_result,
                        DWORD thread_focus_cookie,
                        bool thread_mgr_attempted,
                        HRESULT thread_mgr_result,
                        DWORD thread_mgr_cookie);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_ACTIVATION_H_
