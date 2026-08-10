// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_SDL_MESSAGE_HOOK_H_
#define CXXIME_HOST_TAKEOVER_TSF_SDL_MESSAGE_HOOK_H_

#include <windows.h>

namespace cxxime_tsf {

void trace_sdl_windows_message_hook(HMODULE module);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_SDL_MESSAGE_HOOK_H_
