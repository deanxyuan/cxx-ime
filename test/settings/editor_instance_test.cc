// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_instance.h"

#include <string>

#include "support/background_test_image.h"

namespace {
DWORD probe(const std::filesystem::path& directory) {
    wchar_t executable[32768]{};
    ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, 32768));
    std::wstring command =
        L"\"" + std::wstring(executable) + L"\" --probe \"" + directory.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &process));
    ASSERT_EQ(WaitForSingleObject(process.hProcess, 10000), WAIT_OBJECT_0);
    DWORD result = 99;
    ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &result));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}
} // namespace

TEST(EditorInstance, excludes_another_process_until_drafts_are_destroyed) {
    background_test::Files files;
    const auto other = files.directory / L"other";
    std::filesystem::create_directory(other);
    {
        cxxime::settings::EditorInstance instance;
        ASSERT_TRUE(static_cast<bool>(instance));
        ASSERT_EQ(probe(files.directory), 2u);
        ASSERT_EQ(probe(other), 0u);
    }
    // A stale zero-byte file is not a lock; a new process can reopen it.
    ASSERT_TRUE(std::filesystem::exists(files.directory / L"settings.lock"));
    ASSERT_EQ(probe(files.directory), 0u);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--probe") {
        cxxime::set_user_data_dir(argv[2]);
        cxxime::settings::EditorInstance instance;
        return instance ? 0 : (instance.error() == ERROR_SHARING_VIOLATION ? 2 : 3);
    }
    return test::RunAllTests();
}
