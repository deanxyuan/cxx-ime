// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "custom_phrase_panel.h"

#include <algorithm>
#include <string>
#include <utility>

#include <commctrl.h>
#include <commdlg.h>

#include <cxxime/input_limits.h>
#include <cxxime/user_dict_validation.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {
namespace {

enum ControlId {
    kQuery = 1,
    kSearch,
    kList,
    kCode,
    kText,
    kAdd,
    kSave,
    kDelete,
    kUp,
    kDown,
    kClearForm,
    kImport,
    kExport,
    kPrevious,
    kNext,
};

LRESULT CALLBACK query_edit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                 UINT_PTR subclass_id, DWORD_PTR) {
    if (message == WM_GETDLGCODE && wparam == VK_RETURN) {
        return DLGC_WANTALLKEYS;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(kSearch, BN_CLICKED), 0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, query_edit_proc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

} // namespace

CustomPhrasePanel::CustomPhrasePanel(LexiconControlClient client)
    : client_(std::move(client)) {}

void CustomPhrasePanel::create(HWND parent, int x, int y, int width, int height) {
    WNDCLASSW type = {};
    type.lpfnWndProc = window_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = L"CxxIMECustomPhrasePanel";
    RegisterClassW(&type);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"", WS_CHILD, x, y, width,
                              height, parent, nullptr, type.hInstance, this);
    if (window_) {
        create_controls(width, height);
    }
}

void CustomPhrasePanel::create_controls(int width, int height) {
    auto control = [&](const wchar_t* type, const wchar_t* label, int id, int x, int y, int w,
                       int h, DWORD style) {
        HWND result = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                      window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      GetModuleHandleW(nullptr), nullptr);
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
        return result;
    };
    auto button = [&](int id, const wchar_t* label, int x, int y, int w) {
        return control(L"BUTTON", label, id, x, y, w, kCtrlH, WS_TABSTOP | BS_PUSHBUTTON);
    };
    query_ = control(L"EDIT", L"", kQuery, 0, 0, width - S(76), kCtrlH,
                     WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
    SetWindowSubclass(query_, query_edit_proc, kQuery, 0);
    SendMessageW(query_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"编码或内容"));
    button(kSearch, L"查询", width - S(68), 0, S(68));
    const int form_y = height - S(142);
    list_ = control(WC_LISTVIEWW, L"", kList, 0, kRowH, width, form_y - kRowH - S(8),
                    WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    const wchar_t* labels[] = {L"编码", L"顺序", L"内容"};
    const int widths[] = {S(100), S(48), width - S(152)};
    for (int index = 0; index < 3; ++index) {
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(labels[index]);
        column.cx = widths[index];
        ListView_InsertColumn(list_, index, &column);
    }
    control(L"STATIC", L"编码", 0, 0, form_y + S(3), S(36), kCtrlH, 0);
    code_ = control(L"EDIT", L"", kCode, S(40), form_y, S(110), kCtrlH,
                    WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
    SendMessageW(code_, EM_SETLIMITTEXT, kMaxInputCodeLength, 0);
    SendMessageW(code_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"例如 jtdz"));
    control(L"STATIC", L"内容", 0, S(160), form_y + S(3), S(36), kCtrlH, 0);
    text_ = control(L"EDIT", L"", kText, S(200), form_y, width - S(200), kCtrlH,
                    WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL);
    SendMessageW(text_, EM_SETLIMITTEXT, kCandidateTextCapacity - 1, 0);
    SendMessageW(text_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"地址、邮箱或常用文本"));
    const wchar_t* actions[] = {L"新增", L"保存修改", L"删除", L"上移", L"下移", L"清空输入"};
    for (int index = 0; index < 6; ++index) {
        button(kAdd + index, actions[index], S(index * 78), form_y + kRowH, S(72));
    }
    button(kImport, L"导入", 0, form_y + 2 * kRowH, S(64));
    button(kExport, L"导出", S(72), form_y + 2 * kRowH, S(64));
    button(kPrevious, L"上一页", width - S(144), form_y + 2 * kRowH, S(68));
    button(kNext, L"下一页", width - S(68), form_y + 2 * kRowH, S(68));
    control(L"STATIC", L"各输入模式共用；完整输入编码后选词上屏，修改立即生效。", 0, 0,
            height - S(44), width, S(20), 0);
    status_ = control(L"STATIC", L"", 0, 0, height - S(22), width, S(20), 0);
    update_actions();
}

void CustomPhrasePanel::refresh() {
    LexiconControlRequest request;
    request.operation = LexiconOperation::kQuery;
    request.query = edit_text_utf8(query_);
    request.offset = offset_;
    request.limit = LEXICON_CONTROL_DEFAULT_LIMIT;
    LexiconControlResult result;
    available_ = client_.custom_phrase_request(request, &result);
    if (available_ && offset_ > 0 && result.query.entries.empty()) {
        offset_ = 0;
        refresh();
        return;
    }
    updating_ = true;
    selected_ = -1;
    rows_ = available_ ? std::move(result.query.entries) : std::vector<UserDictEntryInfo>{};
    has_more_ = available_ && result.query.has_more;
    ListView_DeleteAllItems(list_);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        const auto& entry = rows_[index];
        std::wstring code = utf8_to_wstr(entry.code);
        std::wstring text = utf8_to_wstr(entry.text);
        std::wstring rank = std::to_wstring(entry.sequence);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = code.data();
        ListView_InsertItem(list_, &item);
        ListView_SetItemText(list_, item.iItem, 1, rank.data());
        ListView_SetItemText(list_, item.iItem, 2, text.data());
    }
    updating_ = false;
    const std::wstring status = available_ ? L"共 " + std::to_wstring(result.query.resource_total) +
                                                 L" 条；匹配 " +
                                                 std::to_wstring(result.query.match_total) + L" 条"
                                           : L"无法连接输入法服务，请启动 CxxIME 后重试查询。";
    summary_ = status;
    update_actions();
}

void CustomPhrasePanel::update_actions() {
    if (!window_) {
        return;
    }
    const std::string text = edit_text_utf8(text_);
    const std::string code = edit_text_utf8(code_);
    const bool valid = is_valid_user_dict_entry(text, code);
    const wchar_t* notice = summary_.c_str();
    if (available_ && !code.empty() && !is_valid_user_dict_code(code)) {
        notice = L"编码须为 1~64 个小写英文字母。";
    } else if (available_ && !text.empty() && !is_valid_user_dict_text(text)) {
        notice = L"内容须为单行文本，UTF-8 长度不超过 255 字节。";
    }
    SetWindowTextW(status_, notice);
    const bool selected = selected_ >= 0 && selected_ < static_cast<int>(rows_.size());
    const bool duplicate = std::any_of(rows_.begin(), rows_.end(), [&](const auto& row) {
        return row.code == code && row.text == text;
    });
    EnableWindow(GetDlgItem(window_, kAdd), available_ && valid && !duplicate);
    const bool changed =
        selected && (rows_[selected_].code != code || rows_[selected_].text != text);
    EnableWindow(GetDlgItem(window_, kSave), available_ && valid && changed && !duplicate);
    EnableWindow(GetDlgItem(window_, kDelete), available_ && selected);
    EnableWindow(GetDlgItem(window_, kUp), available_ && selected && rows_[selected_].sequence > 1);
    EnableWindow(GetDlgItem(window_, kDown),
                 available_ && selected &&
                     rows_[selected_].sequence < rows_[selected_].same_code_count);
    EnableWindow(GetDlgItem(window_, kImport), available_);
    EnableWindow(GetDlgItem(window_, kExport), available_);
    EnableWindow(GetDlgItem(window_, kPrevious), available_ && offset_ > 0);
    EnableWindow(GetDlgItem(window_, kNext), available_ && has_more_);
}

void CustomPhrasePanel::select() {
    if (updating_) {
        return;
    }
    selected_ = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) {
        SetWindowTextW(code_, utf8_to_wstr(rows_[selected_].code).c_str());
        SetWindowTextW(text_, utf8_to_wstr(rows_[selected_].text).c_str());
    }
    update_actions();
}

bool CustomPhrasePanel::execute(LexiconControlRequest request) {
    LexiconControlResult result;
    if (!client_.custom_phrase_request(std::move(request), &result)) {
        MessageBoxW(window_, L"操作失败，请检查是否重复、条目是否仍存在，以及文件是否可写。",
                    L"自定义短语", MB_OK | MB_ICONWARNING);
        return false;
    }
    refresh();
    return true;
}

void CustomPhrasePanel::transfer_file(bool importing) {
    wchar_t path[MAX_PATH] = L"custom_phrases.tsv";
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"自定义短语 (*.tsv)\0*.tsv\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"tsv";
    dialog.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    if (!(importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog))) {
        return;
    }
    LexiconControlRequest request;
    request.operation = importing ? LexiconOperation::kImport : LexiconOperation::kExport;
    request.source_path = wstr_to_utf8(path);
    if (execute(request)) {
        SetWindowTextW(status_, importing ? L"已合并导入，重复条目保留原有顺序。" : L"已导出。");
    }
}

void CustomPhrasePanel::command(int id) {
    if (id == kCode || id == kText) {
        update_actions();
        return;
    }
    if (id == kSearch || id == kPrevious || id == kNext) {
        if (id == kNext && !has_more_) {
            return;
        }
        offset_ = id == kNext ? offset_ + LEXICON_CONTROL_DEFAULT_LIMIT
                  : id == kPrevious && offset_ >= LEXICON_CONTROL_DEFAULT_LIMIT
                      ? offset_ - LEXICON_CONTROL_DEFAULT_LIMIT
                      : 0;
        refresh();
        return;
    }
    if (id == kImport || id == kExport) {
        transfer_file(id == kImport);
        return;
    }
    if (id == kClearForm) {
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
        selected_ = -1;
        SetWindowTextW(code_, L"");
        SetWindowTextW(text_, L"");
        SetFocus(code_);
        return;
    }
    LexiconControlRequest request;
    request.code = edit_text_utf8(code_);
    request.text = edit_text_utf8(text_);
    if (id == kAdd) {
        request.operation = LexiconOperation::kAdd;
    } else if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) {
        const auto& entry = rows_[selected_];
        if (id == kSave) {
            request.operation = LexiconOperation::kReplace;
            request.old_code = entry.code;
            request.old_text = entry.text;
        } else if (id == kDelete) {
            if (MessageBoxW(window_, L"删除选中的自定义短语？", L"自定义短语",
                            MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return;
            }
            request.operation = LexiconOperation::kDelete;
            request.entries.push_back({entry.text, entry.code});
        } else if (id == kUp || id == kDown) {
            request.operation = id == kUp ? LexiconOperation::kMoveUp : LexiconOperation::kMoveDown;
            request.code = entry.code;
            request.text = entry.text;
        }
    }
    if (request.operation == LexiconOperation::kUnknown) {
        return;
    }
    if (execute(request)) {
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            if (rows_[index].code == request.code && rows_[index].text == request.text) {
                ListView_SetItemState(list_, static_cast<int>(index), LVIS_SELECTED, LVIS_SELECTED);
                ListView_EnsureVisible(list_, static_cast<int>(index), FALSE);
                break;
            }
        }
    }
}

LRESULT CALLBACK CustomPhrasePanel::window_proc(HWND window, UINT message, WPARAM wparam,
                                                LPARAM lparam) {
    auto* panel = reinterpret_cast<CustomPhrasePanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<CustomPhrasePanel*>(
            reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (panel && message == WM_COMMAND) {
        panel->command(LOWORD(wparam));
        return 0;
    }
    if (panel && message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header->idFrom == kList && header->code == LVN_ITEMCHANGED) {
            panel->select();
        }
        return 0;
    }
    if (panel && message == WM_NCDESTROY) {
        panel->window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace settings
} // namespace cxxime
