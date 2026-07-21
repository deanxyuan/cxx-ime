// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/stage_trace.h>

#include <cxxime/diagnostics_config.h>

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace cxxime {
namespace {

std::atomic<uint64_t> g_stage_trace_sequence{0};
std::atomic<uint64_t> g_stage_trace_id{0};
std::mutex g_stage_trace_mutex;

std::string wide_to_utf8(const wchar_t* text) {
    if (!text || !text[0]) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], required, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string current_process_name() {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) {
        return {};
    }
    const wchar_t* base = wcsrchr(path, L'\\');
    return wide_to_utf8(base ? base + 1 : path);
}

std::wstring trace_directory() {
    wchar_t override_path[32768] = {};
    const DWORD override_len = GetEnvironmentVariableW(
        L"CXXIME_STAGE_TRACE_DIR", override_path, ARRAYSIZE(override_path));
    if (override_len > 0 && override_len < ARRAYSIZE(override_path)) {
        CreateDirectoryW(override_path, nullptr);
        return override_path;
    }

    wchar_t profile[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile))) {
        return {};
    }

    std::wstring root(profile);
    root += L"\\cxxime";
    CreateDirectoryW(root.c_str(), nullptr);
    root += L"\\logs";
    CreateDirectoryW(root.c_str(), nullptr);
    return root;
}

std::wstring component_filename(const char* component) {
    std::wstring result;
    const std::string source = component ? component : "unknown";
    result.reserve(source.size());
    for (const unsigned char ch : source) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            result.push_back(static_cast<wchar_t>(ch));
        } else {
            result.push_back(L'_');
        }
    }
    return result;
}

std::wstring trace_path(const char* component) {
    std::wstring directory = trace_directory();
    if (directory.empty()) {
        return {};
    }

    const std::wstring arch = strcmp(stage_trace_arch(), "x64") == 0 ? L"x64" : L"x86";
    const std::wstring filename = L"stage1-" + component_filename(component) + L"-" +
                                  std::to_wstring(GetCurrentProcessId()) + L"-" + arch +
                                  L".jsonl";
    return directory + L"\\" + filename;
}

uint64_t timestamp_100ns() {
    FILETIME value = {};
    GetSystemTimeAsFileTime(&value);
    ULARGE_INTEGER combined = {};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

void rotate_if_needed(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return;
    }

    ULARGE_INTEGER size = {};
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    const auto config = diagnostics_config();
    if (size.QuadPart < config.log_max_size) {
        return;
    }

    const std::wstring rotated = path + L".1";
    DeleteFileW(rotated.c_str());
    MoveFileExW(path.c_str(), rotated.c_str(), MOVEFILE_REPLACE_EXISTING);
}

uint64_t mix64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

} // namespace

const char* stage_trace_build_id() {
    return kStageTraceBuildId;
}

const char* stage_trace_arch() {
#ifdef _WIN64
    return "x64";
#else
    return "x86";
#endif
}

uint64_t stage_trace_next_id() {
    const uint64_t local = g_stage_trace_id.fetch_add(1, std::memory_order_relaxed) + 1;
    return (static_cast<uint64_t>(GetCurrentProcessId()) << 32) | (local & 0xffffffffULL);
}

uint64_t stage_trace_input_id(uint32_t key_code, intptr_t key_data) {
    UNREFERENCED_PARAMETER(key_data);
    const uint64_t thread = static_cast<uint64_t>(GetCurrentThreadId());
    const uint64_t message_time = static_cast<uint32_t>(GetMessageTime());
    return mix64((thread << 32) ^ message_time ^ (static_cast<uint64_t>(key_code) << 16));
}

std::string stage_trace_guid(REFGUID guid) {
    char buffer[64] = {};
    snprintf(buffer, sizeof(buffer),
             "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
             guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
             guid.Data4[6], guid.Data4[7]);
    return buffer;
}

std::string stage_trace_digest_utf16(const wchar_t* text, size_t length) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD copied = 0;
    std::vector<UCHAR> object;
    std::vector<UCHAR> digest;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        return {};
    }
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                               &copied, 0);
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size),
                                   &copied, 0);
    }
    if (status >= 0) {
        object.resize(object_size);
        digest.resize(hash_size);
        status = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                                  nullptr, 0, 0);
    }
    if (status >= 0 && text && length > 0) {
        const size_t byte_length = length * sizeof(wchar_t);
        if (byte_length > static_cast<size_t>(ULONG_MAX)) {
            status = static_cast<NTSTATUS>(-1);
        } else {
            status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(text)),
                                    static_cast<ULONG>(byte_length), 0);
        }
    }
    if (status >= 0) {
        status = BCryptFinishHash(hash, digest.data(), hash_size, 0);
    }

    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        return {};
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(digest.size() * 2, '0');
    for (size_t index = 0; index < digest.size(); ++index) {
        result[index * 2] = kHex[digest[index] >> 4];
        result[index * 2 + 1] = kHex[digest[index] & 0x0f];
    }
    return result;
}

std::string stage_trace_digest_utf16(const std::wstring& text) {
    return stage_trace_digest_utf16(text.data(), text.size());
}

void write_stage_trace(const char* component, const char* event, nlohmann::json fields) {
    if (diagnostics_config().trace_mode == DiagnosticTraceMode::kOff) {
        return;
    }
    if (!fields.is_object()) {
        fields = nlohmann::json::object();
    }

    fields["schema_version"] = kStageTraceSchemaVersion;
    fields["build_id"] = kStageTraceBuildId;
    fields["stage"] = kStageTraceStage;
    fields["arch"] = stage_trace_arch();
    fields["event"] = event ? event : "";
    fields["seq"] = g_stage_trace_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    fields["timestamp_100ns"] = timestamp_100ns();
    fields["pid"] = GetCurrentProcessId();
    fields["tid"] = GetCurrentThreadId();
    fields["process"] = current_process_name();
    fields["component"] = component ? component : "unknown";

    std::string line;
    try {
        line = fields.dump();
    } catch (...) {
        return;
    }

    const std::wstring path = trace_path(component);
    if (path.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_stage_trace_mutex);
    rotate_if_needed(path);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || !file) {
        return;
    }
    fwrite(line.data(), 1, line.size(), file);
    fputc('\n', file);
    fclose(file);
}

} // namespace cxxime
