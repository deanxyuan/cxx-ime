// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_HOST_CLASSIFICATION_MESSAGE_H_
#define CXXIME_TSF_HOST_CLASSIFICATION_MESSAGE_H_

#include <windows.h>

namespace cxxime_tsf {

void preflight_stage_host_classification_compatibility(HWND hwnd);
void trace_stage_host_classification_message_gate(const MSG& message);

} // namespace cxxime_tsf

#endif // CXXIME_TSF_HOST_CLASSIFICATION_MESSAGE_H_
