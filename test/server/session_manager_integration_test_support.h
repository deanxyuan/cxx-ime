// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TEST_SESSION_MANAGER_INTEGRATION_TEST_SUPPORT_H_
#define CXXIME_TEST_SESSION_MANAGER_INTEGRATION_TEST_SUPPORT_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <windows.h>

#include <cxxime/data_path.h>
#include <cxxime/dictionary_manifest.h>
#include <cxxime/dictionary_monitor.h>
#include <cxxime/ipc_protocol.h>
#include <cxxime/key_event.h>
#include <cxxime/lexicon_control.h>
#include <cxxime/short_code_cache.h>
#include <cxxime/spellings_index.h>
#include <cxxime/user_dict_validation.h>

#include "lexicon_control_handler.h"
#include "session_manager.h"
#include "support/testutil.h"
#include "support/topn_test_data.h"
#include "support/wubi_index_test_data.h"

using TestDictEntry = std::tuple<std::string, std::string, int>;

extern char temp_path[MAX_PATH];
extern std::string test_user_data_dir;

std::string make_temp_path(const char* name);
std::string read_text_file(const std::string& path);
void write_manifest_for_files(const std::string& dict_path,
                              const std::vector<std::pair<const char*, std::string>>& files);
void create_test_dictionary_bundle(const std::string& dict_path,
                                   const std::vector<TestDictEntry>& entries);
void create_test_dictionary_bundle_with_wubi(const std::string& dict_path,
                                             const std::vector<TestDictEntry>& pinyin_entries,
                                             const std::vector<TestDictEntry>& wubi_entries);
void delete_test_dictionary_bundle(const std::string& dict_path);
std::string setup_test_dict();
cxxime::KeyEvent make_key(uint32_t vk, bool shift = false, bool caps = false);
ProcessKeyResult type_kao(SessionManager& manager, uint32_t id);
bool candidate_contains(const cxxime::CandidatePresentationPage& page,
                        const std::string& text);
bool wait_for_count(std::atomic<int>& value, int expected, int timeout_ms);

#endif // CXXIME_TEST_SESSION_MANAGER_INTEGRATION_TEST_SUPPORT_H_
