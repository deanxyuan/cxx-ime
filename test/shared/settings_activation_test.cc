// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <imm.h>

#include <cxxime/settings_launcher.h>

#include "support/testutil.h"

namespace {

struct ActivationReport {
    HWND root;
    HWND target;
    DWORD thread;
    WPARAM panel;
    bool enabled;
};

ActivationReport* child_report = nullptr;
HANDLE child_done = nullptr;

// Replace only OS policy/launch boundaries. Window ownership and message delivery
// below use real windows on an isolated desktop, never the installed settings UI.
const HWND kExisting = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1234));
const DWORD kTargetPid = 4242;
const wchar_t kInstallPath[] = L"C:\\CxxIME test";
const wchar_t kExecutable[] = L"C:\\CxxIME test\\cxxime-settings.exe";
std::vector<std::string> calls;
HWND existing = nullptr;
HWND foreground = nullptr;
bool grant_allowed = true;
bool create_allowed = true;
bool resume_allowed = true;
bool post_allowed = true;
bool register_allowed = true;
bool foreground_allowed = true;
HANDLE process_handle = nullptr;
HANDLE thread_handle = nullptr;
DWORD activation_thread = 0;
DWORD expected_pid = kTargetPid;
cxxime::SettingsPanel expected_panel = cxxime::SettingsPanel::kDictionary;
int show_count = 0;
int flash_count = 0;

void reset() {
    calls.clear();
    existing = nullptr;
    foreground = nullptr;
    grant_allowed = create_allowed = resume_allowed = true;
    post_allowed = register_allowed = foreground_allowed = true;
    process_handle = thread_handle = nullptr;
    activation_thread = 0;
    expected_pid = kTargetPid;
    expected_panel = cxxime::SettingsPanel::kDictionary;
    show_count = flash_count = 0;
}

HANDLE WINAPI test_mutex(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCWSTR) {
    return CreateMutexW(attributes, owner, nullptr);
}

LSTATUS WINAPI test_open_key(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY key) {
    *key = reinterpret_cast<HKEY>(static_cast<UINT_PTR>(1));
    return ERROR_SUCCESS;
}

LSTATUS WINAPI test_query_value(HKEY, LPCWSTR, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size) {
    *type = REG_SZ;
    *size = sizeof(kInstallPath);
    memcpy(data, kInstallPath, sizeof(kInstallPath));
    return ERROR_SUCCESS;
}

LSTATUS WINAPI test_close_key(HKEY) { return ERROR_SUCCESS; }
DWORD WINAPI test_attributes(LPCWSTR) { return FILE_ATTRIBUTE_NORMAL; }
HWND WINAPI test_find(HWND, HWND previous, LPCWSTR, LPCWSTR) {
    return previous ? nullptr : existing;
}

DWORD WINAPI test_window_pid(HWND window, LPDWORD pid) {
    if (window == kExisting) {
        *pid = kTargetPid;
        return 1;
    }
    return GetWindowThreadProcessId(window, pid);
}

HANDLE WINAPI test_open_process(DWORD access, BOOL inherit, DWORD pid) {
    return OpenProcess(access, inherit, pid == kTargetPid ? GetCurrentProcessId() : pid);
}

BOOL WINAPI test_image_path(HANDLE, DWORD, LPWSTR buffer, PDWORD size) {
    wcscpy_s(buffer, *size, kExecutable);
    *size = static_cast<DWORD>(wcslen(kExecutable));
    return TRUE;
}

UINT WINAPI test_register(LPCWSTR name) {
    return register_allowed ? RegisterWindowMessageW(name) : 0;
}

BOOL WINAPI test_grant(DWORD pid) {
    ASSERT_TRUE(pid != 0 && pid != ASFW_ANY);
    ASSERT_EQ(pid, expected_pid);
    calls.push_back("grant");
    return grant_allowed;
}

BOOL WINAPI test_post(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    calls.push_back("post");
    if (window == kExisting) {
        ASSERT_EQ(message, RegisterWindowMessageW(cxxime::kSettingsNavigateMessage));
        ASSERT_EQ(wp, static_cast<WPARAM>(cxxime::SettingsPanel::kDictionary));
        return post_allowed;
    }
    return post_allowed && PostMessageW(window, message, wp, lp);
}

BOOL WINAPI test_create(LPCWSTR path, LPWSTR command, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                        BOOL inherit, DWORD flags, LPVOID, LPCWSTR, LPSTARTUPINFOW,
                        LPPROCESS_INFORMATION process) {
    calls.push_back("create");
    ASSERT_EQ(std::wstring(path), std::wstring(kExecutable));
    if (expected_panel == cxxime::SettingsPanel::kDictionary) {
        ASSERT_TRUE(command != nullptr);
        ASSERT_EQ(std::wstring(command),
                  L"\"" + std::wstring(kExecutable) + L"\" --panel dictionary");
    } else {
        ASSERT_TRUE(command == nullptr);
    }
    ASSERT_EQ(inherit, FALSE);
    ASSERT_EQ(flags, static_cast<DWORD>(CREATE_SUSPENDED));
    if (!create_allowed) {
        return FALSE;
    }
    process_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    process->hProcess = process_handle;
    process->hThread = thread_handle;
    process->dwProcessId = kTargetPid;
    return TRUE;
}

DWORD WINAPI test_resume(HANDLE thread) {
    calls.push_back("resume");
    ASSERT_EQ(thread, thread_handle);
    return resume_allowed ? 1 : static_cast<DWORD>(-1);
}

BOOL WINAPI test_terminate(HANDLE process, UINT) {
    calls.push_back("terminate");
    ASSERT_EQ(process, process_handle);
    return TRUE;
}

BOOL WINAPI test_close(HANDLE handle) {
    if (handle == process_handle) {
        calls.push_back("close_process");
    } else if (handle == thread_handle) {
        calls.push_back("close_thread");
    }
    return CloseHandle(handle);
}

BOOL WINAPI test_show(HWND window, int command) {
    ++show_count;
    return ShowWindow(window, command);
}

BOOL WINAPI test_foreground(HWND window) {
    calls.push_back("activate");
    activation_thread = GetCurrentThreadId();
    if (foreground_allowed) {
        foreground = window;
    }
    return foreground_allowed;
}

HWND WINAPI test_get_foreground() { return foreground; }
BOOL WINAPI test_flash(PFLASHWINFO info) {
    ASSERT_EQ(info->dwFlags, static_cast<DWORD>(FLASHW_TRAY));
    ++flash_count;
    return FALSE;
}

} // namespace

// Compile the real launcher against narrow WinAPI substitutes, without adding
// injectable callbacks or test switches to production code.
#define CreateMutexW               test_mutex
#define RegOpenKeyExW              test_open_key
#define RegQueryValueExW           test_query_value
#define RegCloseKey                test_close_key
#define GetFileAttributesW         test_attributes
#define FindWindowExW              test_find
#define GetWindowThreadProcessId   test_window_pid
#define OpenProcess                test_open_process
#define QueryFullProcessImageNameW test_image_path
#define RegisterWindowMessageW     test_register
#define AllowSetForegroundWindow   test_grant
#define PostMessageW               test_post
#define CreateProcessW             test_create
#define ResumeThread               test_resume
#define TerminateProcess           test_terminate
#define CloseHandle                test_close
#define ShowWindow                 test_show
#define SetForegroundWindow        test_foreground
#define GetForegroundWindow        test_get_foreground
#define FlashWindowEx              test_flash
#include "../../shared/src/settings_launcher.cc"
#undef CreateMutexW
#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef RegCloseKey
#undef GetFileAttributesW
#undef FindWindowExW
#undef GetWindowThreadProcessId
#undef OpenProcess
#undef QueryFullProcessImageNameW
#undef RegisterWindowMessageW
#undef AllowSetForegroundWindow
#undef PostMessageW
#undef CreateProcessW
#undef ResumeThread
#undef TerminateProcess
#undef CloseHandle
#undef ShowWindow
#undef SetForegroundWindow
#undef GetForegroundWindow
#undef FlashWindowEx

namespace {

void expect_calls(std::initializer_list<const char*> expected) {
    ASSERT_EQ(calls.size(), expected.size());
    size_t index = 0;
    for (const char* value : expected) {
        ASSERT_EQ(calls[index++], std::string(value));
    }
}

HWND create_window(HWND owner = nullptr) {
    HWND window =
        CreateWindowExW(0, L"STATIC", L"CxxIME isolated activation test", WS_OVERLAPPEDWINDOW, 10,
                        10, 200, 100, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(window != nullptr);
    return window;
}

template <typename Callback>
void on_isolated_desktop(Callback callback) {
    const std::wstring name = L"CxxIME-Settings-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    ASSERT_TRUE(desktop != nullptr);
    std::thread thread([&]() {
        HDESK original = GetThreadDesktop(GetCurrentThreadId());
        ASSERT_TRUE(SetThreadDesktop(desktop));
        callback();
        ASSERT_TRUE(SetThreadDesktop(original));
    });
    thread.join();
    ASSERT_TRUE(CloseDesktop(desktop));
}

LRESULT CALLBACK receiver(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_TIMER) {
        PostQuitMessage(1);
        return 0;
    }
    if (message == RegisterWindowMessageW(cxxime::kSettingsNavigateMessage)) {
        child_report->panel = wp;
        child_report->enabled = IsWindowEnabled(window) != FALSE;
        ASSERT_TRUE(cxxime::activate_settings_window(window));
        child_report->target = foreground;
        child_report->thread = activation_thread;
        SetEvent(child_done);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

int receive_activation(const char* mapping_arg, const char* ready_arg, const char* done_arg) {
    HANDLE mapping = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(mapping_arg)));
    HANDLE ready = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(ready_arg)));
    child_done = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(done_arg)));
    child_report = static_cast<ActivationReport*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ActivationReport)));
    ASSERT_TRUE(child_report != nullptr);
    WNDCLASSW type = {};
    type.lpfnWndProc = receiver;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"CxxIME-Activation-Receiver-Test";
    ASSERT_TRUE(RegisterClassW(&type));
    HWND root =
        CreateWindowW(type.lpszClassName, L"Isolated settings receiver", WS_OVERLAPPEDWINDOW, 0, 0,
                      200, 100, nullptr, nullptr, type.hInstance, nullptr);
    ASSERT_TRUE(root != nullptr);
    ShowWindow(root, SW_SHOW);
    HWND modal = create_window(root);
    EnableWindow(root, FALSE);
    ShowWindow(modal, SW_SHOW);
    SetActiveWindow(modal);
    child_report->root = root;
    // The test harness exits immediately on assertions. Bound the child lifetime
    // even if its parent fails before it can post WM_QUIT.
    ASSERT_TRUE(SetTimer(root, 1, 15000, nullptr) != 0);
    SetEvent(ready);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(modal);
    DestroyWindow(root);
    UnmapViewOfFile(child_report);
    CloseHandle(mapping);
    CloseHandle(ready);
    CloseHandle(child_done);
    return static_cast<int>(message.wParam);
}

} // namespace

TEST(SettingsActivation, first_launch_grants_before_running_and_closes_every_handle) {
    reset();
    expected_panel = cxxime::SettingsPanel::kInput;
    ASSERT_TRUE(cxxime::open_settings());
    expect_calls({"create", "grant", "resume", "close_process", "close_thread"});
    for (bool allowed : {true, false}) {
        reset();
        grant_allowed = allowed;
        ASSERT_TRUE(cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
        expect_calls({"create", "grant", "resume", "close_process", "close_thread"});
    }
    reset();
    create_allowed = false;
    ASSERT_TRUE(!cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
    expect_calls({"create"});
    reset();
    resume_allowed = false;
    ASSERT_TRUE(!cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
    expect_calls({"create", "grant", "resume", "terminate", "close_process", "close_thread"});
}

TEST(SettingsActivation, existing_request_grants_before_posting_and_never_launches_again) {
    for (bool allowed : {true, false}) {
        reset();
        existing = kExisting;
        grant_allowed = allowed;
        ASSERT_TRUE(cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
        expect_calls({"grant", "post"});
        ASSERT_EQ(show_count, 0);
    }
    reset();
    existing = kExisting;
    post_allowed = false;
    ASSERT_TRUE(!cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
    expect_calls({"grant", "post"});
    reset();
    existing = kExisting;
    register_allowed = false;
    ASSERT_TRUE(!cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
    expect_calls({});
}

TEST(SettingsActivation, restores_only_when_needed_and_denial_never_retries) {
    on_isolated_desktop([]() {
        reset();
        HWND root = create_window();
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_TRUE(IsWindowVisible(root));
        ASSERT_EQ(show_count, 1);
        ASSERT_EQ(foreground, root);
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_EQ(show_count, 1);
        ShowWindow(root, SW_MINIMIZE);
        ASSERT_TRUE(IsIconic(root));
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_TRUE(!IsIconic(root));
        ASSERT_EQ(show_count, 2);
        reset();
        foreground_allowed = false;
        ASSERT_TRUE(!cxxime::activate_settings_window(root));
        expect_calls({"activate"});
        ASSERT_EQ(show_count, 0);
        ASSERT_EQ(flash_count, 1);
        ASSERT_TRUE((GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
        DestroyWindow(root);
    });
}

TEST(SettingsActivation, nested_modal_is_activated_without_enabling_its_owners) {
    on_isolated_desktop([]() {
        reset();
        HWND root = create_window();
        ShowWindow(root, SW_SHOW);
        HWND modal = create_window(root);
        EnableWindow(root, FALSE);
        ShowWindow(modal, SW_SHOW);
        SetActiveWindow(modal);
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_EQ(foreground, modal);
        HWND nested = create_window(modal);
        EnableWindow(modal, FALSE);
        ShowWindow(nested, SW_SHOW);
        SetActiveWindow(nested);
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_EQ(foreground, nested);
        ASSERT_TRUE(!IsWindowEnabled(root) && !IsWindowEnabled(modal));
        ShowWindow(root, SW_MINIMIZE);
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_TRUE(!IsIconic(root));
        ASSERT_EQ(foreground, nested);
        ShowWindow(nested, SW_HIDE);
        reset();
        ASSERT_TRUE(!cxxime::activate_settings_window(root));
        expect_calls({});
        ASSERT_TRUE(!IsWindowEnabled(root));
        DestroyWindow(nested);
        DestroyWindow(modal);
        EnableWindow(root, TRUE);
        ASSERT_TRUE(cxxime::activate_settings_window(root));
        ASSERT_EQ(foreground, root);
        DestroyWindow(root);
    });
}

TEST(SettingsActivation, cross_process_request_activates_on_the_receiver_ui_thread) {
    reset();
    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                        sizeof(ActivationReport), nullptr);
    HANDLE ready = CreateEventW(&security, TRUE, FALSE, nullptr);
    HANDLE done = CreateEventW(&security, TRUE, FALSE, nullptr);
    ASSERT_TRUE(mapping && ready && done);
    auto* report = static_cast<ActivationReport*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ActivationReport)));
    ASSERT_TRUE(report != nullptr);
    const std::wstring name = L"CxxIME-Settings-Child-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    ASSERT_TRUE(desktop != nullptr);
    wchar_t executable[32768] = {};
    ASSERT_TRUE(GetModuleFileNameW(nullptr, executable, 32768));
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --receive " +
                           std::to_wstring(reinterpret_cast<UINT_PTR>(mapping)) + L" " +
                           std::to_wstring(reinterpret_cast<UINT_PTR>(ready)) + L" " +
                           std::to_wstring(reinterpret_cast<UINT_PTR>(done));
    STARTUPINFOW startup = {sizeof(startup)};
    startup.lpDesktop = const_cast<wchar_t*>(name.c_str());
    PROCESS_INFORMATION process = {};
    ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &process));
    ASSERT_EQ(WaitForSingleObject(ready, 10000), WAIT_OBJECT_0);
    existing = report->root;
    expected_pid = process.dwProcessId;
    std::thread sender([&]() {
        HDESK original = GetThreadDesktop(GetCurrentThreadId());
        ASSERT_TRUE(SetThreadDesktop(desktop));
        ASSERT_TRUE(cxxime::open_settings(cxxime::SettingsPanel::kDictionary));
        ASSERT_EQ(WaitForSingleObject(done, 10000), WAIT_OBJECT_0);
        ASSERT_TRUE(PostThreadMessageW(process.dwThreadId, WM_QUIT, 0, 0));
        ASSERT_TRUE(SetThreadDesktop(original));
    });
    sender.join();
    expect_calls({"grant", "post"});
    ASSERT_EQ(show_count, 0);
    ASSERT_EQ(report->panel, static_cast<WPARAM>(cxxime::SettingsPanel::kDictionary));
    ASSERT_TRUE(!report->enabled);
    ASSERT_TRUE(report->target != nullptr && report->target != report->root);
    ASSERT_EQ(report->thread, process.dwThreadId);
    ASSERT_EQ(WaitForSingleObject(process.hProcess, 10000), WAIT_OBJECT_0);
    DWORD result = 1;
    ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &result));
    ASSERT_EQ(result, 0u);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    UnmapViewOfFile(report);
    CloseHandle(mapping);
    CloseHandle(ready);
    CloseHandle(done);
    ASSERT_TRUE(CloseDesktop(desktop));
}

int main(int argc, char** argv) {
    // These windows test settings activation, not text input. Prevent an installed
    // IME from attaching helper threads/windows to the isolated test desktops.
    ASSERT_TRUE(ImmDisableIME(static_cast<DWORD>(-1)));
    if (argc == 5 && std::string(argv[1]) == "--receive") {
        return receive_activation(argv[2], argv[3], argv[4]);
    }
    return test::RunAllTests();
}
