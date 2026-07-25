// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_
#define CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_

#include <cxxime/stage_trace.h>

#include <msctf.h>

namespace cxxime_tsf {

void add_stage_ui_element_identity_fields(ITfUIElement* element, nlohmann::json& fields);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_IDENTITY_H_
