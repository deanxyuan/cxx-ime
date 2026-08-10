// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/installer_prompt.h>

#include <array>

#include <windows.h>
#include <commctrl.h>

namespace cxxime {
namespace installer {
namespace {

constexpr int kButtonRetry = 1001;
constexpr int kButtonDeferUntilRestart = 1002;
constexpr int kButtonCancel = 1003;

const wchar_t* window_title(LockPromptMode mode) {
    if (mode == LockPromptMode::kInstall) {
        return L"CxxIME 安装程序";
    }
    return L"CxxIME 卸载程序";
}

const wchar_t* main_instruction(LockPromptMode mode, LockQueryStatus status) {
    if (status == LockQueryStatus::kFailed) {
        return L"无法检查 CxxIME 文件占用情况";
    }
    if (status == LockQueryStatus::kRebootRequired) {
        return L"需要重新启动 Windows";
    }
    if (mode == LockPromptMode::kInstall) {
        return L"无法更新正在使用的 CxxIME 文件";
    }
    return L"部分 CxxIME 文件正在使用";
}

} // namespace

LockPromptChoice show_lock_prompt(LockPromptMode mode,
                                  std::uintptr_t parent_window,
                                  const LockQueryResult& result,
                                  const std::wstring& report) {
    const std::array<TASKDIALOG_BUTTON, 2> common_buttons = {{
        {kButtonRetry, L"重试\n关闭占用 CxxIME 的应用程序后重新检查。"},
        {kButtonCancel, L"取消\n关闭当前操作，不进行更改。"},
    }};
    const std::array<TASKDIALOG_BUTTON, 3> uninstall_buttons = {{
        {kButtonRetry, L"重试\n关闭占用 CxxIME 的应用程序后重新检查。"},
        {kButtonDeferUntilRestart,
         L"重启后完成卸载\n立即停用 CxxIME，并在 Windows 重启后删除占用的文件。"},
        {kButtonCancel, L"取消\n关闭当前操作，不进行更改。"},
    }};

    const bool allow_deferred =
        mode == LockPromptMode::kUninstall && result.status != LockQueryStatus::kFailed;
    const TASKDIALOG_BUTTON* buttons = common_buttons.data();
    UINT button_count = static_cast<UINT>(common_buttons.size());
    if (allow_deferred) {
        buttons = uninstall_buttons.data();
        button_count = static_cast<UINT>(uninstall_buttons.size());
    }

    HWND parent = reinterpret_cast<HWND>(parent_window);
    if (!IsWindow(parent)) {
        parent = nullptr;
    }

    TASKDIALOGCONFIG config = {};
    config.cbSize = sizeof(config);
    config.hwndParent = parent;
    config.dwFlags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_USE_COMMAND_LINKS;
    config.pszWindowTitle = window_title(mode);
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = main_instruction(mode, result.status);
    config.pszContent = report.c_str();
    config.cButtons = button_count;
    config.pButtons = buttons;
    config.nDefaultButton = kButtonRetry;

    int selected_button = 0;
    const HRESULT hr = TaskDialogIndirect(&config, &selected_button, nullptr, nullptr);
    if (FAILED(hr)) {
        return LockPromptChoice::kFailed;
    }
    if (selected_button == kButtonRetry) {
        return LockPromptChoice::kRetry;
    }
    if (selected_button == kButtonDeferUntilRestart && allow_deferred) {
        return LockPromptChoice::kDeferUntilRestart;
    }
    return LockPromptChoice::kCancel;
}

} // namespace installer
} // namespace cxxime
