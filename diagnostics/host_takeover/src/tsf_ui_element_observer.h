// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_OBSERVER_H_
#define CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_OBSERVER_H_

#include <msctf.h>

namespace cxxime_tsf {

void start_ui_element_observer(ITfThreadMgr* thread_mgr, DWORD activate_flags);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_UI_ELEMENT_OBSERVER_H_
