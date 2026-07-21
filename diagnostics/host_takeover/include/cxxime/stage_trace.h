// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_STAGE_TRACE_H_
#define CXXIME_STAGE_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include <guiddef.h>
#include <json.hpp>

namespace cxxime {

inline constexpr int kStageTraceSchemaVersion = 1;
inline constexpr int kStageTraceStage = 1;
inline constexpr char kStageTraceBuildId[] = "dota2-stage1-20260720-a";

const char* stage_trace_build_id();
const char* stage_trace_arch();

uint64_t stage_trace_next_id();
uint64_t stage_trace_input_id(uint32_t key_code, intptr_t key_data);
std::string stage_trace_guid(REFGUID guid);
std::string stage_trace_digest_utf16(const wchar_t* text, size_t length);
std::string stage_trace_digest_utf16(const std::wstring& text);

void write_stage_trace(const char* component,
                       const char* event,
                       nlohmann::json fields = nlohmann::json::object());

} // namespace cxxime

#endif // CXXIME_STAGE_TRACE_H_
