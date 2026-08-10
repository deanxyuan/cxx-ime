// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_
#define CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_

#include <cxxime/host_trace.h>

#include <msctf.h>

namespace cxxime_tsf {

void add_ui_element_identity_fields(ITfUIElement* element, nlohmann::json& fields);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_
