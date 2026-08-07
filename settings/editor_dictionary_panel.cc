// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <thread>

#include <commdlg.h>
#include <shellapi.h>

#include <cxxime/data_path.h>
#include <cxxime/pipe_names.h>
#include <cxxime/user_dict_control.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {
namespace {

struct UserDictQueryCompletion {
    UserDictKind kind = UserDictKind::PINYIN;
    bool succeeded = false;
    UserDictControlResult result;
};

bool control_pipe_is_absent() {
    const std::wstring pipe_name = make_user_pipe_name(CONTROL_PIPE_BASE_NAME);
    return !WaitNamedPipeW(pipe_name.c_str(), 1) && GetLastError() == ERROR_FILE_NOT_FOUND;
}

LRESULT CALLBACK QueryEditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                               UINT_PTR subclass_id, DWORD_PTR reference_data) {
    if (message == WM_GETDLGCODE && wparam == VK_RETURN) {
        return DLGC_WANTALLKEYS;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SendMessageW(reinterpret_cast<HWND>(reference_data), WM_COMMAND,
                     MAKEWPARAM(4001, BN_CLICKED), reinterpret_cast<LPARAM>(window));
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, QueryEditProc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

std::wstring file_last_write_time_text(const std::string& path) {
    std::wstring wide_path = path_for_display(path);
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(wide_path.c_str(), GetFileExInfoStandard, &data)) {
        return L"未创建";
    }

    FILETIME local_time = {};
    SYSTEMTIME system_time = {};
    if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local_time) ||
        !FileTimeToSystemTime(&local_time, &system_time)) {
        return L"未知";
    }

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u", system_time.wYear, system_time.wMonth,
               system_time.wDay, system_time.wHour, system_time.wMinute);
    return buffer;
}

bool copy_file_utf8_path(const std::string& source, const std::string& destination) {
    std::wstring wide_source = path_for_display(source);
    std::wstring wide_destination = path_for_display(destination);
    return CopyFileW(wide_source.c_str(), wide_destination.c_str(), FALSE) != FALSE;
}

} // namespace

void EditorApp::create_dictionary_panel(HWND panel, int panel_width) {
    const int top = kPanelPadTop;
    SetWindowSubclass(panel, PanelForwardProc, 4000, reinterpret_cast<DWORD_PTR>(hwnd_));

    int control_x = make_label(L"词典:", kPanelPadLeft, top, panel);
    hDictKind_ = make_combo(4013, control_x, top, S(110), panel);
    combo_add(hDictKind_, L"拼音");
    combo_add(hDictKind_, L"五笔");
    combo_sel(hDictKind_, L"拼音");

    hDictStatus_ = CreateWindowExW(0, L"STATIC", L"用户词典: 连接中...",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
                                   control_x + S(128), top, panel_width - control_x - S(144),
                                   kCtrlH, panel, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictStatus_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    int query_y = top + kRowH;
    control_x = make_label(L"查询:", kPanelPadLeft, query_y, panel);
    hDictQuery_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        control_x, query_y, S(190), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4000)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictQuery_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    SetWindowSubclass(hDictQuery_, QueryEditProc, 4000, reinterpret_cast<DWORD_PTR>(hwnd_));

    auto make_button = [&](int id, const wchar_t* text, int x, int y, int width) {
        HWND button = CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y, width,
            kCtrlH, panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
             GetModuleHandle(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
        return button;
    };
    make_button(4001, L"查询", control_x + S(202), query_y, S(68));
    make_button(4002, L"刷新", control_x + S(278), query_y, S(68));

    int list_y = query_y + kRowH;
    int list_width = panel_width - kPanelPadLeft - S(10);
    int list_height = S(132);
    hDictList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kPanelPadLeft, list_y, list_width, list_height, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4003)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictList_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    ListView_SetExtendedListViewStyle(hDictList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPWSTR>(L"编码");
    column.cx = S(120);
    ListView_InsertColumn(hDictList_, 0, &column);
    column.pszText = const_cast<LPWSTR>(L"词语");
    column.cx = list_width - S(200);
    column.iSubItem = 1;
    ListView_InsertColumn(hDictList_, 1, &column);
    column.pszText = const_cast<LPWSTR>(L"频率");
    column.cx = S(70);
    column.iSubItem = 2;
    ListView_InsertColumn(hDictList_, 2, &column);

    int edit_y = list_y + list_height + S(8);
    int edit_width = S(170);
    control_x = make_label(L"词语:", kPanelPadLeft, edit_y, panel);
    hDictText_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        control_x, edit_y, edit_width, kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4004)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictText_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    int code_x = control_x + edit_width + S(14);
    int code_control_x = make_label(L"编码:", code_x, edit_y, panel);
    hDictCode_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        code_control_x, edit_y, S(120), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4005)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictCode_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    int action_y = edit_y + kRowH;
    int button_x = kPanelPadLeft;
    hDictAdd_ = make_button(4006, L"新增", button_x, action_y, S(64));
    hDictSave_ = make_button(4007, L"保存修改", button_x + S(72), action_y, S(86));
    hDictDelete_ = make_button(4008, L"删除选中", button_x + S(166), action_y, S(86));
    hDictClear_ = make_button(4009, L"取消编辑", button_x + S(260), action_y, S(86));

    int file_y = action_y + kRowH;
    make_button(4010, L"导入", kPanelPadLeft, file_y, S(64));
    make_button(4011, L"导出", kPanelPadLeft + S(72), file_y, S(64));
    make_button(4012, L"打开目录", kPanelPadLeft + S(144), file_y, S(86));

    hDictUserPath_ =
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS,
                        kPanelPadLeft, file_y + kRowH, panel_width - kPanelPadLeft - S(10), kCtrlH,
                        panel, nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hDictUserPath_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    hDictTooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
                                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, panel, nullptr,
                                    GetModuleHandle(nullptr), nullptr);
    TOOLINFOW tool = {sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = panel;
    tool.uId = reinterpret_cast<UINT_PTR>(hDictUserPath_);
    tool.lpszText = const_cast<LPWSTR>(L"");
    SendMessageW(hDictTooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    update_user_entry_actions();
}

bool EditorApp::handle_dictionary_command(int control_id, int notification) {
    switch (control_id) {
    case 4001:
        query_user_entries();
        return true;
    case 4002:
        refresh_user_entries();
        return true;
    case 4004:
    case 4005:
        if (notification == EN_CHANGE) {
            update_user_entry_actions();
        }
        return true;
    case 4006:
        add_user_entry();
        return true;
    case 4007:
        save_user_entry();
        return true;
    case 4008:
        delete_user_entry();
        return true;
    case 4009:
        clear_user_entry_form();
        return true;
    case 4010:
        import_user_dict();
        return true;
    case 4011:
        export_user_dict();
        return true;
    case 4012:
        open_user_dict_dir();
        return true;
    case 4013:
        if (notification == CBN_SELCHANGE) {
            clear_user_entry_form();
            refresh_user_entries();
        }
        return true;
    default:
        return false;
    }
}

bool EditorApp::handle_dictionary_notify(LPARAM notification) {
    auto* header = reinterpret_cast<LPNMHDR>(notification);
    if (!header || header->idFrom != 4003 || header->code != LVN_ITEMCHANGED) {
        return false;
    }
    auto* item = reinterpret_cast<LPNMLISTVIEW>(notification);
    if ((item->uChanged & LVIF_STATE) == 0) {
        return false;
    }
    on_user_entry_selected();
    return true;
}

UserDictKind EditorApp::current_user_dict_kind() const {
    int index = combo_index(hDictKind_);
    return index == 1 ? UserDictKind::WUBI : UserDictKind::PINYIN;
}

std::string EditorApp::current_user_dict_path() const {
    return user_data_path(current_user_dict_kind() == UserDictKind::WUBI ? "user_wubi.tsv"
                                                                         : "user_pinyin.tsv");
}

void EditorApp::update_user_dict_status() {
    std::string path = current_user_dict_path();
    update_user_dict_path();
    if (!hDictStatus_) {
        return;
    }

    std::wstring modified = file_last_write_time_text(path);
    int shown = hDictList_ ? ListView_GetItemCount(hDictList_) : 0;
    wchar_t buffer[160] = {};
    swprintf_s(buffer, L"显示 %d 条，更新于 %s", shown, modified.c_str());
    SetWindowTextW(hDictStatus_, buffer);
}

void EditorApp::update_user_dict_path() {
    if (!hDictUserPath_) {
        return;
    }
    dictPathTooltip_ = L"用户词典: " + path_for_display(current_user_dict_path());
    SetWindowTextW(hDictUserPath_, dictPathTooltip_.c_str());
    if (hDictTooltip_) {
        TOOLINFOW tool = {sizeof(tool)};
        tool.hwnd = hPanels_[4];
        tool.uId = reinterpret_cast<UINT_PTR>(hDictUserPath_);
        tool.lpszText = const_cast<LPWSTR>(dictPathTooltip_.c_str());
        SendMessageW(hDictTooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void EditorApp::update_user_entry_actions() {
    bool has_text = hDictText_ && GetWindowTextLengthW(hDictText_) > 0;
    bool has_code = hDictCode_ && GetWindowTextLengthW(hDictCode_) > 0;
    bool selected = !selectedDictText_.empty();
    if (hDictAdd_) {
        EnableWindow(hDictAdd_, has_text && has_code && !selected);
    }
    if (hDictSave_) {
        EnableWindow(hDictSave_, has_text && has_code && selected);
    }
    if (hDictDelete_) {
        EnableWindow(hDictDelete_, selected);
    }
    if (hDictClear_) {
        EnableWindow(hDictClear_, has_text || has_code || selected);
    }
}

void EditorApp::refresh_user_entries() {
    if (hDictQuery_) {
        SetWindowTextW(hDictQuery_, L"");
    }
    query_user_entries();
}

void EditorApp::set_user_dict_status(const std::wstring& text) {
    if (hDictStatus_) {
        SetWindowTextW(hDictStatus_, text.c_str());
    }
}

void EditorApp::query_user_entries() {
    if (!hDictList_) {
        return;
    }

    selectedDictText_.clear();
    selectedDictCode_.clear();
    SetWindowTextW(hDictText_, L"");
    SetWindowTextW(hDictCode_, L"");
    ListView_DeleteAllItems(hDictList_);
    update_user_entry_actions();

    const std::string query = edit_text_utf8(hDictQuery_);
    const UserDictKind kind = current_user_dict_kind();
    const WPARAM generation = ++dictQueryGeneration_;
    const HWND window = hwnd_;
    update_user_dict_path();
    if (control_pipe_is_absent()) {
        set_user_dict_status(L"CxxIME 后台未运行");
        return;
    }
    set_user_dict_status(L"正在连接 CxxIME 后台...");

    std::thread([window, generation, kind, query]() {
        UserDictQueryCompletion completion;
        completion.kind = kind;
        UserDictControlClient client;
        completion.succeeded = client.query(kind, query, 0, USER_DICT_CONTROL_DEFAULT_LIMIT,
                                            &completion.result);
        SendMessageW(window, kUserDictQueryCompleteMessage, generation,
                     reinterpret_cast<LPARAM>(&completion));
    }).detach();
}

void EditorApp::handle_user_dict_query_complete(WPARAM generation, LPARAM completion_data) {
    const auto* completion = reinterpret_cast<const UserDictQueryCompletion*>(completion_data);
    if (!completion || generation != dictQueryGeneration_ ||
        completion->kind != current_user_dict_kind()) {
        return;
    }

    if (!completion->succeeded) {
        set_user_dict_status(L"CxxIME 后台不可用");
        return;
    }

    const UserDictControlResult& result = completion->result;
    for (size_t i = 0; i < result.query.entries.size(); ++i) {
        std::wstring code = utf8_to_wstr(result.query.entries[i].code);
        std::wstring text = utf8_to_wstr(result.query.entries[i].text);
        wchar_t frequency[32] = {};
        swprintf_s(frequency, L"%d", result.query.entries[i].frequency);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(code.c_str());
        int row = ListView_InsertItem(hDictList_, &item);
        ListView_SetItemText(hDictList_, row, 1, const_cast<LPWSTR>(text.c_str()));
        ListView_SetItemText(hDictList_, row, 2, frequency);
    }

    int shown = ListView_GetItemCount(hDictList_);
    if (result.query.dictionary_total == 0) {
        set_user_dict_status(L"暂无用户词条");
        update_user_dict_path();
        return;
    }

    std::wstring modified = file_last_write_time_text(current_user_dict_path());
    wchar_t buffer[192] = {};
    if (modified == L"未创建") {
        swprintf_s(buffer, L"共 %zu 条，显示 %d 条", result.query.dictionary_total, shown);
    } else {
        swprintf_s(buffer, L"共 %zu 条，显示 %d 条，更新于 %s", result.query.dictionary_total,
            shown, modified.c_str());
    }
    SetWindowTextW(hDictStatus_, buffer);
    update_user_dict_path();
}

void EditorApp::clear_user_entry_form() {
    selectedDictText_.clear();
    selectedDictCode_.clear();
    if (hDictText_) {
        SetWindowTextW(hDictText_, L"");
    }
    if (hDictCode_) {
        SetWindowTextW(hDictCode_, L"");
    }
    if (hDictList_) {
        ListView_SetItemState(hDictList_, -1, 0, LVIS_SELECTED);
    }
    update_user_entry_actions();
    set_user_dict_status(L"已取消编辑");
}

void EditorApp::add_user_entry() {
    std::string text = edit_text_utf8(hDictText_);
    std::string code = edit_text_utf8(hDictCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    UserDictControlClient client;
    UserDictControlResult result;
    if (!client.add_entry(current_user_dict_kind(), text, code, &result)) {
        MessageBoxW(hwnd_, L"新增词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    clear_user_entry_form();
    query_user_entries();
    set_user_dict_status(L"已新增词条，正在刷新...");
}

void EditorApp::save_user_entry() {
    if (selectedDictText_.empty()) {
        MessageBoxW(hwnd_, L"请先在列表中选择一个词条。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string old_text = wstr_to_utf8(selectedDictText_);
    std::string old_code = wstr_to_utf8(selectedDictCode_);
    std::string text = edit_text_utf8(hDictText_);
    std::string code = edit_text_utf8(hDictCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    UserDictControlClient client;
    UserDictControlResult result;
    if (!client.replace_entry(current_user_dict_kind(), old_text, old_code, text, code, &result)) {
        MessageBoxW(hwnd_, L"保存修改失败。可能存在同名用户词条。", L"CxxIME",
                    MB_OK | MB_ICONERROR);
        return;
    }
    selectedDictText_ = utf8_to_wstr(text);
    selectedDictCode_ = utf8_to_wstr(code);
    query_user_entries();
    set_user_dict_status(L"已保存修改，正在刷新...");
}

void EditorApp::delete_user_entry() {
    if (selectedDictText_.empty()) {
        MessageBoxW(hwnd_, L"请先在列表中选择一个词条。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring message = L"删除用户词条 \"" + selectedDictText_ + L"\"？";
    if (MessageBoxW(hwnd_, message.c_str(), L"CxxIME", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    std::string text = wstr_to_utf8(selectedDictText_);
    std::string code = wstr_to_utf8(selectedDictCode_);
    UserDictControlClient client;
    UserDictControlResult result;
    if (!client.delete_entry(current_user_dict_kind(), text, code, &result)) {
        MessageBoxW(hwnd_, L"删除词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    clear_user_entry_form();
    query_user_entries();
    set_user_dict_status(L"已删除词条，正在刷新...");
}

void EditorApp::import_user_dict() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"TSV 用户词典 (*.tsv)\0*.tsv\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }
    if (MessageBoxW(hwnd_, L"导入会覆盖当前用户词典，是否继续？", L"CxxIME",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    std::string source = wstr_to_utf8(file);
    std::string destination = current_user_dict_path();
    if (!copy_file_utf8_path(source, destination)) {
        MessageBoxW(hwnd_, L"导入失败，无法复制词典文件。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    UserDictControlClient client;
    UserDictControlResult result;
    bool reloaded = client.reload(current_user_dict_kind(), &result);
    clear_user_entry_form();
    if (reloaded) {
        query_user_entries();
        set_user_dict_status(L"导入完成，正在刷新...");
        MessageBoxW(hwnd_, L"用户词典已导入并重新加载。", L"CxxIME", MB_OK | MB_ICONINFORMATION);
    } else {
        set_user_dict_status(L"已复制词典文件，但服务未能立即重新加载");
        MessageBoxW(hwnd_,
            L"用户词典文件已导入，但 CxxIME 服务未能立即重新加载。"
            L"重新切换输入法或重启服务后会使用新词典。",
            L"CxxIME", MB_OK | MB_ICONWARNING);
    }
}

void EditorApp::export_user_dict() {
    wchar_t file[MAX_PATH] = {};
    wcscpy_s(file, current_user_dict_kind() == UserDictKind::WUBI ? L"user_wubi.tsv"
                                                                  : L"user_pinyin.tsv");
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"TSV 用户词典 (*.tsv)\0*.tsv\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"tsv";
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }
    UserDictControlClient client;
    UserDictControlResult result;
    bool saved = client.save(current_user_dict_kind(), &result);
    std::string source = current_user_dict_path();
    std::string destination = wstr_to_utf8(file);
    if (!copy_file_utf8_path(source, destination)) {
        MessageBoxW(hwnd_, L"导出失败，无法复制词典文件。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    update_user_dict_status();
    std::wstring message = L"用户词典已导出到:\n" + path_for_display(destination);
    if (!saved) {
        message += L"\n\n注意: 服务未能立即保存最新内存状态，已导出现有词典文件。";
    }
    MessageBoxW(hwnd_, message.c_str(), L"CxxIME", MB_OK | MB_ICONINFORMATION);
}

void EditorApp::open_user_dict_dir() {
    std::wstring directory = path_for_display(user_data_dir());
    HINSTANCE result =
        ShellExecuteW(hwnd_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        std::wstring message = L"无法打开用户词典目录:\n" + directory;
        MessageBoxW(hwnd_, message.c_str(), L"CxxIME", MB_OK | MB_ICONERROR);
    }
}

void EditorApp::on_user_entry_selected() {
    if (!hDictList_) {
        return;
    }
    int row = ListView_GetNextItem(hDictList_, -1, LVNI_SELECTED);
    if (row < 0) {
        selectedDictCode_.clear();
        selectedDictText_.clear();
        SetWindowTextW(hDictCode_, L"");
        SetWindowTextW(hDictText_, L"");
        update_user_entry_actions();
        return;
    }
    wchar_t code[64] = {};
    wchar_t text[128] = {};
    ListView_GetItemText(hDictList_, row, 0, code, 64);
    ListView_GetItemText(hDictList_, row, 1, text, 128);
    selectedDictCode_ = code;
    selectedDictText_ = text;
    SetWindowTextW(hDictCode_, code);
    SetWindowTextW(hDictText_, text);
    update_user_entry_actions();
}

} // namespace settings
} // namespace cxxime
