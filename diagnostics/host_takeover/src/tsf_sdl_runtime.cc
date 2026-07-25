// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_sdl_runtime.h"
#include "tsf_sdl_message_hook.h"

#include <cxxime/stage_trace.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace cxxime_tsf {
namespace {

constexpr char kImeImplementedUiHint[] = "SDL_IME_IMPLEMENTED_UI";
constexpr uint32_t kSdlEventTextEditingCandidates = 0x307;

struct SdlTextEditingCandidatesEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t window_id;
    const char* const* candidates;
    int32_t candidate_count;
    int32_t selected_candidate;
    bool horizontal;
    uint8_t padding[3];
};

using SdlGetVersion = int(__cdecl*)();
using SdlGetRevision = const char*(__cdecl*)();
using SdlGetHint = const char*(__cdecl*)(const char* name);
using SdlEventFilter = bool(__cdecl*)(void* userdata, void* event);
using SdlAddEventWatch = bool(__cdecl*)(SdlEventFilter filter, void* userdata);
using SdlRemoveEventWatch = void(__cdecl*)(SdlEventFilter filter, void* userdata);
using SdlEventEnabled = bool(__cdecl*)(uint32_t event_type);

std::atomic<bool> g_event_watch_installed{false};

template <typename Function>
Function load_sdl_export(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

bool contains_capability(const std::string& hint, const char* capability) {
    return hint.find(capability) != std::string::npos;
}

bool __cdecl trace_sdl_event(void*, void* raw_event) {
    const auto* event = static_cast<const SdlTextEditingCandidatesEvent*>(raw_event);
    if (!event || event->type != kSdlEventTextEditingCandidates) {
        return true;
    }

    cxxime::write_stage_trace("tsf", "sdl.candidate_event", {
        {"event_type", event->type},
        {"timestamp_ns", event->timestamp},
        {"window_id", event->window_id},
        {"candidates_present", event->candidates != nullptr},
        {"candidate_count", event->candidate_count},
        {"selected_candidate", event->selected_candidate},
        {"horizontal", event->horizontal},
        {"result", "observed"},
    });
    return true;
}

void start_sdl_event_watch(HMODULE module, bool handles_candidates) {
    const auto add_event_watch =
        load_sdl_export<SdlAddEventWatch>(module, "SDL_AddEventWatch");
    const auto event_enabled =
        load_sdl_export<SdlEventEnabled>(module, "SDL_EventEnabled");
    const bool candidate_event_enabled =
        event_enabled && event_enabled(kSdlEventTextEditingCandidates);
    const bool transition_capture = stage_profile_transition_capture_requested();
    const bool should_install = handles_candidates || transition_capture;
    const bool already_installed = g_event_watch_installed.load();
    bool installed = already_installed;
    if (should_install && add_event_watch && !already_installed) {
        installed = add_event_watch(trace_sdl_event, nullptr);
        if (installed) {
            g_event_watch_installed.store(true);
        }
    }

    const char* result = "capability_disabled";
    if (already_installed) {
        result = "already_installed";
    } else if (!add_event_watch) {
        result = "export_unavailable";
    } else if (should_install) {
        result = installed ? "installed" : "install_failed";
    }
    cxxime::write_stage_trace("tsf", "sdl.event_watch", {
        {"action", "start"},
        {"candidate_event_type", kSdlEventTextEditingCandidates},
        {"candidate_event_enabled_export_present", event_enabled != nullptr},
        {"candidate_event_enabled", candidate_event_enabled},
        {"add_event_watch_export_present", add_event_watch != nullptr},
        {"profile_transition_capture", transition_capture},
        {"result", result},
    });
}

} // namespace

bool stage_profile_transition_capture_requested() {
    return true;
}

void trace_stage_sdl_runtime() {
    HMODULE module = GetModuleHandleW(L"SDL3.dll");
    if (!module) {
        cxxime::write_stage_trace("tsf", "sdl.runtime", {
            {"module", "SDL3.dll"},
            {"result", "module_not_loaded"},
        });
        return;
    }

    const auto get_version = load_sdl_export<SdlGetVersion>(module, "SDL_GetVersion");
    const auto get_revision = load_sdl_export<SdlGetRevision>(module, "SDL_GetRevision");
    const auto get_hint = load_sdl_export<SdlGetHint>(module, "SDL_GetHint");

    const int version = get_version ? get_version() : 0;
    const char* revision_value = get_revision ? get_revision() : nullptr;
    const char* hint_value = get_hint ? get_hint(kImeImplementedUiHint) : nullptr;
    const std::string revision = revision_value ? revision_value : "";
    const std::string hint = hint_value ? hint_value : "";

    const bool handles_candidates = contains_capability(hint, "candidates");
    cxxime::write_stage_trace("tsf", "sdl.runtime", {
        {"module", "SDL3.dll"},
        {"version_export_present", get_version != nullptr},
        {"version", version},
        {"version_major", version / 1000000},
        {"version_minor", (version / 1000) % 1000},
        {"version_micro", version % 1000},
        {"revision_export_present", get_revision != nullptr},
        {"revision", revision},
        {"hint_export_present", get_hint != nullptr},
        {"ime_implemented_ui_present", hint_value != nullptr},
        {"ime_implemented_ui", hint},
        {"handles_candidates", handles_candidates},
        {"handles_composition", contains_capability(hint, "composition")},
        {"result", get_version && get_revision && get_hint ? "queried" : "partial"},
    });
    trace_stage_sdl_windows_message_hook(module);
    start_sdl_event_watch(module, handles_candidates);
}

void stop_stage_sdl_event_watch() {
    if (!g_event_watch_installed.exchange(false)) {
        return;
    }

    HMODULE module = GetModuleHandleW(L"SDL3.dll");
    const auto remove_event_watch = module
        ? load_sdl_export<SdlRemoveEventWatch>(module, "SDL_RemoveEventWatch")
        : nullptr;
    if (remove_event_watch) {
        remove_event_watch(trace_sdl_event, nullptr);
    }
    cxxime::write_stage_trace("tsf", "sdl.event_watch", {
        {"action", "stop"},
        {"remove_event_watch_export_present", remove_event_watch != nullptr},
        {"result", remove_event_watch ? "removed" : "remove_unavailable"},
    });
}

} // namespace cxxime_tsf
