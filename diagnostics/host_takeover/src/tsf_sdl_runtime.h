// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_SDL_RUNTIME_H_
#define CXXIME_HOST_TAKEOVER_TSF_SDL_RUNTIME_H_

namespace cxxime_tsf {

bool stage_profile_transition_capture_requested();
void trace_stage_sdl_runtime();
void stop_stage_sdl_event_watch();

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_SDL_RUNTIME_H_
