// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <string>

#include <cxxime/lexicon_control.h>

#include "editor_app_internal.h"

namespace cxxime {
namespace settings {

void EditorApp::add_lexicon_entry() {
    const std::string text = edit_text_utf8(hLexiconText_);
    const std::string code = edit_text_utf8(hLexiconCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    LexiconControlClient client;
    LexiconControlResult result;
    if (!client.add_entry(current_user_dict_kind(), text, code, &result)) {
        MessageBoxW(hwnd_, L"新增词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    clear_lexicon_entry_form();
    query_lexicon_entries(false);
}

void EditorApp::save_lexicon_entry() {
    if (!selectedLexiconHasUser_) {
        return;
    }
    const std::string text = edit_text_utf8(hLexiconText_);
    const std::string code = edit_text_utf8(hLexiconCode_);
    if (text.empty() || code.empty()) {
        MessageBoxW(hwnd_, L"请输入词语和编码。", L"CxxIME", MB_OK | MB_ICONWARNING);
        return;
    }
    LexiconControlClient client;
    LexiconControlResult result;
    if (!client.replace_entry(current_user_dict_kind(), wstr_to_utf8(selectedLexiconText_),
                              wstr_to_utf8(selectedLexiconCode_), text, code, &result)) {
        MessageBoxW(hwnd_, L"保存修改失败，可能存在相同的用户词条。", L"CxxIME",
                    MB_OK | MB_ICONERROR);
        return;
    }
    clear_lexicon_entry_form();
    query_lexicon_entries(false);
}

void EditorApp::delete_lexicon_entries() {
    if (!lexiconServerAvailable_ || lexiconImportRunning_ || lexiconQueryRunning_) {
        return;
    }
    const std::vector<LexiconEntryKey> entries =
        selected_user_entry_keys(lexiconRows_, selected_lexicon_row_indices());
    if (entries.empty()) {
        return;
    }
    const bool preference = current_lexicon_resource() == LexiconResource::kCandidatePreference;
    wchar_t count[32] = {};
    swprintf_s(count, L"%zu", entries.size());
    std::wstring message = L"删除选中的 ";
    message += count;
    message += preference ? L" 项选词偏好？" : L" 个用户词？";
    if (!preference && selectedLexiconCount_ > entries.size()) {
        wchar_t retained[32] = {};
        swprintf_s(retained, L"%zu", selectedLexiconCount_ - entries.size());
        message += L"\n\n另外 ";
        message += retained;
        message += L" 个仅系统词不会受影响。";
    }
    if (MessageBoxW(hwnd_, message.c_str(), L"CxxIME", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    LexiconControlClient client;
    LexiconControlResult result;
    const bool deleted = preference
                             ? client.delete_preferences(current_user_dict_kind(), entries, &result)
                             : client.delete_entries(current_user_dict_kind(), entries, &result);
    if (!deleted) {
        MessageBoxW(hwnd_, L"删除词条失败。", L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    clear_lexicon_entry_form();
    query_lexicon_entries(false);
}

void EditorApp::disable_or_restore_system_entry() {
    if (!selectedLexiconHasSystem_ || !lexiconDisabledStateAvailable_) {
        return;
    }
    LexiconControlClient client;
    LexiconControlResult result;
    const std::string text = wstr_to_utf8(selectedLexiconText_);
    const bool succeeded =
        selectedLexiconSystemDisabled_
            ? client.restore_system_entry(current_user_dict_kind(), text, &result)
            : client.disable_system_entry(current_user_dict_kind(), text, &result);
    if (!succeeded) {
        MessageBoxW(hwnd_,
                    selectedLexiconSystemDisabled_ ? L"恢复系统词失败。" : L"隐藏系统词失败。",
                    L"CxxIME", MB_OK | MB_ICONERROR);
        return;
    }
    query_lexicon_entries(false);
}

} // namespace settings
} // namespace cxxime
