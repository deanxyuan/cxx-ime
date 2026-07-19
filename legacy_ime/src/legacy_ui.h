// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_LEGACY_IME_LEGACY_UI_H_
#define CXXIME_LEGACY_IME_LEGACY_UI_H_

#include <windows.h>

namespace cxxime_legacy {

bool register_ui_class(HINSTANCE instance);
void unregister_ui_class(HINSTANCE instance);

} // namespace cxxime_legacy

#endif // CXXIME_LEGACY_IME_LEGACY_UI_H_
