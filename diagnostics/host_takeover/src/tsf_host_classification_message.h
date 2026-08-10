// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_HOST_CLASSIFICATION_MESSAGE_H_
#define CXXIME_HOST_TAKEOVER_TSF_HOST_CLASSIFICATION_MESSAGE_H_

#include <windows.h>

namespace cxxime_tsf {

void preflight_host_classification_compatibility(HWND hwnd);
void trace_host_classification_message_gate(const MSG& message);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_HOST_CLASSIFICATION_MESSAGE_H_
