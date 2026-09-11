// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_LIFECYCLE_INTERNAL_H_
#define CXXIME_INSTALLER_LIFECYCLE_INTERNAL_H_

#include <cstddef>
#include <string>
#include <vector>

#include <cxxime/installer_lifecycle.h>

namespace cxxime {
namespace installer {
namespace lifecycle_internal {

struct State {
    std::string active;
    std::string prepared;
    std::vector<std::string> retired;
};

void set_error(unsigned long* error_code, unsigned long value);
bool normalize_path(const std::wstring& path, std::wstring* normalized);
bool generation_from_path(const std::wstring& root, const std::wstring& path,
                          std::string* generation);
std::wstring generation_path(const std::wstring& root, const std::string& generation);
bool write_file_atomically(const std::wstring& path, const void* data, std::size_t size,
                           unsigned long* error_code);
bool ensure_maintenance_directory(const std::wstring& root, unsigned long* error_code);
bool load_state(const std::wstring& root, State* state, unsigned long* error_code);
bool save_state(const std::wstring& root, const State& state, unsigned long* error_code);
void add_retired(State* state, const std::string& generation);
bool reconcile_state(const std::wstring& root, const std::wstring& registered_active, State* state,
                     unsigned long* error_code);
bool discover_retired_generations(const std::wstring& root, State* state,
                                  unsigned long* error_code);
bool allocate_target(const std::wstring& root, const std::string& version,
                     InstallLifecycleResult* result, unsigned long* error_code);
bool collect_garbage(const std::wstring& root, State* state, InstallLifecycleResult* result,
                     unsigned long* error_code);
bool validate_generation(const std::wstring& root, const std::string& generation,
                         unsigned long* error_code);

} // namespace lifecycle_internal
} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_LIFECYCLE_INTERNAL_H_
