// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_callsite.h"

#include <cxxime/stage_trace.h>

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

constexpr USHORT kMaxCallsiteFrames = 24;

std::string wide_to_utf8(const wchar_t* text, int length) {
    if (!text || length <= 0) {
        return {};
    }
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text, length, &result[0], bytes, nullptr, nullptr);
    return result;
}

std::string module_basename(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0) {
        return {};
    }
    DWORD offset = 0;
    for (DWORD index = 0; index < length; ++index) {
        if (path[index] == L'\\' || path[index] == L'/') {
            offset = index + 1;
        }
    }
    return wide_to_utf8(path + offset, static_cast<int>(length - offset));
}

} // namespace

void trace_stage_host_ui_callsite(const char* boundary, bool element_registered) {
    static volatile LONG begin_callback_traced = 0;
    static volatile LONG registered_element_traced = 0;
    volatile LONG* traced =
        element_registered ? &registered_element_traced : &begin_callback_traced;
    if (InterlockedCompareExchange(traced, 1, 0) != 0) {
        return;
    }

    void* callstack[kMaxCallsiteFrames] = {};
    const USHORT frame_count = RtlCaptureStackBackTrace(
        0, kMaxCallsiteFrames, callstack, nullptr);
    nlohmann::json frames = nlohmann::json::array();
    for (USHORT index = 0; index < frame_count; ++index) {
        HMODULE module = nullptr;
        const BOOL mapped = GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(callstack[index]), &module);
        nlohmann::json frame = {
            {"index", index},
            {"mapped", mapped != FALSE},
        };
        if (mapped && module) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(callstack[index]);
            const uintptr_t base = reinterpret_cast<uintptr_t>(module);
            frame["module"] = module_basename(module);
            frame["rva"] = static_cast<uint64_t>(address - base);
        }
        frames.push_back(std::move(frame));
    }

    cxxime::write_stage_trace("tsf", "ui_element.host_callsite", {
        {"boundary", boundary ? boundary : ""},
        {"element_registered", element_registered},
        {"reader_phase", element_registered ? "registered_element" : "begin_callback"},
        {"frame_count", frame_count},
        {"frames", std::move(frames)},
        {"result", frame_count > 0 ? "captured" : "unavailable"},
    });
}

} // namespace cxxime_tsf
