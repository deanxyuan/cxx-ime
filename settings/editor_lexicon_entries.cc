// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <algorithm>
#include <thread>
#include <utility>

#include <cxxime/lexicon_control.h>
#include <cxxime/pipe_names.h>

#include "editor_app_internal.h"
#include "lexicon_query_service.h"

namespace cxxime {
namespace settings {
namespace {

struct LexiconQueryCompletion {
    UserDictKind kind = UserDictKind::PINYIN;
    LexiconResource resource = LexiconResource::kUserLexicon;
    bool server_available = false;
    bool disabled_state_available = false;
    bool system_query_requested = false;
    bool preserve_editor = false;
    LexiconSearchKind search_kind = LexiconSearchKind::kEmpty;
    std::string query;
    std::string selected_text;
    std::string selected_code;
    SystemLexiconQueryResult system_result;
    LexiconControlResult server_result;
    std::vector<LexiconPanelEntry> rows;
};

struct LexiconCodeCompletion {
    UserDictKind kind = UserDictKind::PINYIN;
    std::string text;
    std::string error;
    std::vector<std::string> codes;
};

bool control_pipe_is_absent() {
    const std::wstring pipe_name = make_user_pipe_name(CONTROL_PIPE_BASE_NAME);
    return !WaitNamedPipeW(pipe_name.c_str(), 1) && GetLastError() == ERROR_FILE_NOT_FOUND;
}

SystemLexiconType system_lexicon_type(UserDictKind kind) {
    return kind == UserDictKind::WUBI ? SystemLexiconType::kWubi : SystemLexiconType::kPinyin;
}

const wchar_t* source_label(const LexiconPanelEntry& row) {
    if (row.has_system_source && row.has_user_source) {
        return L"系统 + 用户";
    }
    return row.has_system_source ? L"系统" : L"用户";
}

const wchar_t* status_label(const LexiconPanelEntry& row, bool status_available) {
    if (!row.has_system_source) {
        return L"可用";
    }
    if (!status_available) {
        return L"状态未知";
    }
    if (row.system_disabled) {
        return row.has_user_source ? L"系统已停用" : L"已停用";
    }
    return L"可用";
}

void set_list_text(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<LPWSTR>(text.c_str()));
}

} // namespace

void EditorApp::refresh_lexicon_entries() { query_lexicon_entries(true); }

void EditorApp::query_lexicon_entries(bool preserve_editor) {
    if (!hLexiconList_) {
        return;
    }

    const WPARAM generation = ++lexiconQueryGeneration_;
    const std::string query = edit_text_utf8(hLexiconQuery_);
    const UserDictKind kind = current_user_dict_kind();
    const LexiconResource resource = current_lexicon_resource();
    const std::string selected_text = wstr_to_utf8(selectedLexiconText_);
    const std::string selected_code = wstr_to_utf8(selectedLexiconCode_);
    LexiconSearchKind search_kind = LexiconSearchKind::kEmpty;
    if (resource == LexiconResource::kUserLexicon) {
        search_kind = classify_lexicon_search(query);
        if (search_kind == LexiconSearchKind::kInvalid) {
            lexiconQueryPending_ = false;
            if (!preserve_editor) {
                clear_lexicon_entry_form();
            }
            ListView_DeleteAllItems(hLexiconList_);
            lexiconRows_.clear();
            SetWindowTextW(hLexiconStatus_, L"请输入中文词语或小写字母编码");
            update_lexicon_entry_actions();
            return;
        }
    }

    if (lexiconQueryRunning_) {
        lexiconQueryPending_ = true;
        lexiconQueryPendingPreserveEditor_ = preserve_editor;
        return;
    }
    lexiconQueryRunning_ = true;
    lexiconQueryPending_ = false;

    if (!preserve_editor) {
        clear_lexicon_entry_form();
    }
    ListView_DeleteAllItems(hLexiconList_);
    lexiconRows_.clear();
    update_lexicon_entry_actions();

    const HWND window = hwnd_;
    const bool pipe_absent = control_pipe_is_absent();
    const auto service = lexiconQueryService_;
    SetWindowTextW(hLexiconStatus_, pipe_absent ? L"正在查询系统词典..." : L"正在查询...");

    std::thread([window, generation, kind, resource, search_kind, query, pipe_absent, service,
                 preserve_editor, selected_text, selected_code]() {
        LexiconQueryCompletion completion;
        completion.kind = kind;
        completion.resource = resource;
        completion.preserve_editor = preserve_editor;
        completion.search_kind = search_kind;
        completion.query = query;
        completion.selected_text = selected_text;
        completion.selected_code = selected_code;

        if (resource == LexiconResource::kUserLexicon) {
            completion.system_query_requested = search_kind != LexiconSearchKind::kEmpty;
            if (completion.system_query_requested) {
                completion.system_result = service->query(system_lexicon_type(kind), search_kind,
                                                          query, LEXICON_CONTROL_DEFAULT_LIMIT);
            }
        }

        std::vector<UserDictEntryInfo> user_entries;
        std::vector<UserDictEntryInfo> disabled_entries;
        if (!pipe_absent) {
            LexiconControlClient client;
            completion.server_available = client.query(
                resource, kind, query, 0, LEXICON_CONTROL_DEFAULT_LIMIT, &completion.server_result);
            if (completion.server_available) {
                user_entries = completion.server_result.query.entries;
            }
            if (resource == LexiconResource::kUserLexicon) {
                std::vector<std::string> system_texts;
                for (const auto& entry : completion.system_result.entries) {
                    if (std::find(system_texts.begin(), system_texts.end(), entry.text) ==
                        system_texts.end()) {
                        system_texts.push_back(entry.text);
                    }
                }
                if (system_texts.empty()) {
                    completion.disabled_state_available = completion.server_available;
                } else {
                    LexiconControlResult disabled_result;
                    completion.disabled_state_available =
                        client.query_system_entry_status(kind, system_texts, &disabled_result);
                    if (completion.disabled_state_available) {
                        disabled_entries = std::move(disabled_result.query.entries);
                    }
                }
            }
        }

        if (resource == LexiconResource::kUserLexicon) {
            completion.rows = merge_lexicon_entries(completion.system_result.entries, user_entries,
                                                    disabled_entries);
            if (completion.rows.size() > LEXICON_CONTROL_DEFAULT_LIMIT) {
                completion.rows.resize(LEXICON_CONTROL_DEFAULT_LIMIT);
            }
        } else {
            completion.rows.reserve(user_entries.size());
            for (const auto& entry : user_entries) {
                LexiconPanelEntry row;
                row.text = entry.text;
                row.code = entry.code;
                row.frequency = entry.frequency;
                row.has_user_source = true;
                completion.rows.push_back(std::move(row));
            }
        }

        SendMessageW(window, kLexiconQueryCompleteMessage, generation,
                     reinterpret_cast<LPARAM>(&completion));
    }).detach();
}

void EditorApp::handle_lexicon_query_complete(WPARAM generation, LPARAM completion_data) {
    const auto* completion = reinterpret_cast<const LexiconQueryCompletion*>(completion_data);
    lexiconQueryRunning_ = false;
    if (lexiconQueryPending_) {
        const bool preserve_editor = lexiconQueryPendingPreserveEditor_;
        lexiconQueryPending_ = false;
        query_lexicon_entries(preserve_editor);
    }
    if (!completion || generation != lexiconQueryGeneration_ ||
        completion->kind != current_user_dict_kind() ||
        completion->resource != current_lexicon_resource()) {
        return;
    }

    lexiconServerAvailable_ = completion->server_available;
    lexiconDisabledStateAvailable_ = completion->disabled_state_available;
    lexiconRows_ = completion->rows;
    ListView_DeleteAllItems(hLexiconList_);
    for (std::size_t index = 0; index < lexiconRows_.size(); ++index) {
        const auto& row_data = lexiconRows_[index];
        const std::wstring text = utf8_to_wstr(row_data.text);
        const std::wstring code = utf8_to_wstr(row_data.code);
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<LPWSTR>(text.c_str());
        const int row = ListView_InsertItem(hLexiconList_, &item);
        set_list_text(hLexiconList_, row, 1, code);
        if (completion->resource == LexiconResource::kCandidatePreference) {
            wchar_t frequency[32] = {};
            swprintf_s(frequency, L"%d", row_data.frequency);
            set_list_text(hLexiconList_, row, 2, frequency);
        } else {
            set_list_text(hLexiconList_, row, 2, source_label(row_data));
            set_list_text(hLexiconList_, row, 3,
                          status_label(row_data, lexiconDisabledStateAvailable_));
        }
    }

    const bool prefill_new_entry = should_prefill_new_lexicon_entry(
        completion->search_kind, completion->query, completion->preserve_editor,
        completion->system_result.available, lexiconRows_);
    if (prefill_new_entry) {
        updatingLexiconForm_ = true;
        SetWindowTextW(hLexiconText_, utf8_to_wstr(completion->query).c_str());
        SendMessageW(hLexiconCode_, CB_RESETCONTENT, 0, 0);
        SetWindowTextW(hLexiconCode_, L"");
        updatingLexiconForm_ = false;
        lexiconCodeManuallyEdited_ = false;
        request_lexicon_code_suggestions();
    }

    if (completion->preserve_editor && !completion->selected_text.empty()) {
        const auto selected = std::find_if(lexiconRows_.begin(), lexiconRows_.end(),
                                           [&](const LexiconPanelEntry& row) {
                                               return row.text == completion->selected_text &&
                                                      row.code == completion->selected_code;
                                           });
        if (selected != lexiconRows_.end()) {
            const std::size_t index = static_cast<std::size_t>(selected - lexiconRows_.begin());
            selectedLexiconText_ = utf8_to_wstr(selected->text);
            selectedLexiconCode_ = utf8_to_wstr(selected->code);
            selectedLexiconHasSystem_ = selected->has_system_source;
            selectedLexiconHasUser_ = selected->has_user_source;
            selectedLexiconSystemDisabled_ = selected->system_disabled;
            updatingLexiconForm_ = true;
            ListView_SetItemState(hLexiconList_, static_cast<int>(index),
                                  LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            updatingLexiconForm_ = false;
        } else {
            selectedLexiconText_.clear();
            selectedLexiconCode_.clear();
            selectedLexiconHasSystem_ = false;
            selectedLexiconHasUser_ = false;
            selectedLexiconSystemDisabled_ = false;
        }
    }

    if (completion->resource == LexiconResource::kCandidatePreference) {
        if (!completion->server_available) {
            SetWindowTextW(hLexiconStatus_, L"CxxIME 后台未运行");
        } else if (lexiconRows_.empty()) {
            SetWindowTextW(hLexiconStatus_, L"暂无选词偏好");
        } else {
            wchar_t status[96] = {};
            swprintf_s(status, L"显示 %zu 条选词偏好", lexiconRows_.size());
            SetWindowTextW(hLexiconStatus_, status);
        }
    } else if (completion->system_query_requested && !completion->system_result.available &&
               !completion->server_available) {
        SetWindowTextW(hLexiconStatus_, L"系统词典不可用，CxxIME 后台未运行");
    } else if (completion->system_query_requested && !completion->system_result.available) {
        SetWindowTextW(hLexiconStatus_, L"系统词典不可查询，仅显示用户词条");
    } else if (!completion->server_available) {
        SetWindowTextW(hLexiconStatus_,
                       lexiconRows_.empty() ? L"CxxIME 后台未运行" : L"仅显示系统词条；后台未运行");
    } else if (lexiconRows_.empty()) {
        SetWindowTextW(hLexiconStatus_,
                       prefill_new_entry ? L"没有匹配词条，可以新增" : L"没有匹配词条");
    } else {
        wchar_t status[96] = {};
        swprintf_s(status, L"显示 %zu 条词条", lexiconRows_.size());
        SetWindowTextW(hLexiconStatus_, status);
    }
    update_lexicon_entry_actions();
}

void EditorApp::request_lexicon_code_suggestions() {
    const WPARAM generation = ++lexiconCodeGeneration_;
    SendMessageW(hLexiconCode_, CB_RESETCONTENT, 0, 0);
    if (current_lexicon_resource() != LexiconResource::kUserLexicon) {
        return;
    }
    const std::string text = edit_text_utf8(hLexiconText_);
    if (classify_lexicon_search(text) != LexiconSearchKind::kText) {
        lexiconCodePending_ = false;
        return;
    }

    if (lexiconCodeRunning_) {
        lexiconCodePending_ = true;
        return;
    }
    lexiconCodeRunning_ = true;
    lexiconCodePending_ = false;

    const UserDictKind kind = current_user_dict_kind();
    const HWND window = hwnd_;
    const auto service = lexiconQueryService_;
    std::thread([window, generation, kind, text, service]() {
        LexiconCodeCompletion completion;
        completion.kind = kind;
        completion.text = text;
        completion.codes =
            service->suggest_codes(system_lexicon_type(kind), text, 8, &completion.error);
        SendMessageW(window, kLexiconCodeCompleteMessage, generation,
                     reinterpret_cast<LPARAM>(&completion));
    }).detach();
}

void EditorApp::handle_lexicon_code_complete(WPARAM generation, LPARAM completion_data) {
    const auto* completion = reinterpret_cast<const LexiconCodeCompletion*>(completion_data);
    lexiconCodeRunning_ = false;
    if (lexiconCodePending_) {
        lexiconCodePending_ = false;
        request_lexicon_code_suggestions();
    }
    if (!completion || generation != lexiconCodeGeneration_ ||
        completion->kind != current_user_dict_kind() ||
        completion->text != edit_text_utf8(hLexiconText_)) {
        return;
    }

    const std::wstring current_code = utf8_to_wstr(edit_text_utf8(hLexiconCode_));
    updatingLexiconForm_ = true;
    SendMessageW(hLexiconCode_, CB_RESETCONTENT, 0, 0);
    for (const auto& code : completion->codes) {
        const std::wstring wide_code = utf8_to_wstr(code);
        SendMessageW(hLexiconCode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide_code.c_str()));
    }
    if (!lexiconCodeManuallyEdited_ && !completion->codes.empty()) {
        SetWindowTextW(hLexiconCode_, utf8_to_wstr(completion->codes.front()).c_str());
    } else {
        SetWindowTextW(hLexiconCode_, current_code.c_str());
    }
    updatingLexiconForm_ = false;
    if (completion->codes.empty()) {
        SetWindowTextW(hLexiconStatus_, completion->error.empty()
                                            ? L"未找到可用编码，请手动输入"
                                            : L"系统词典不可用，无法自动生成编码");
    }
    update_lexicon_entry_actions();
}

void EditorApp::clear_lexicon_entry_form() {
    KillTimer(hwnd_, kLexiconCodeTimerId);
    ++lexiconCodeGeneration_;
    lexiconCodePending_ = false;
    selectedLexiconText_.clear();
    selectedLexiconCode_.clear();
    selectedLexiconHasSystem_ = false;
    selectedLexiconHasUser_ = false;
    selectedLexiconSystemDisabled_ = false;
    lexiconCodeManuallyEdited_ = false;
    updatingLexiconForm_ = true;
    SetWindowTextW(hLexiconText_, L"");
    SendMessageW(hLexiconCode_, CB_RESETCONTENT, 0, 0);
    SetWindowTextW(hLexiconCode_, L"");
    ListView_SetItemState(hLexiconList_, -1, 0, LVIS_SELECTED);
    updatingLexiconForm_ = false;
    update_lexicon_entry_actions();
}

void EditorApp::on_lexicon_entry_selected() {
    if (updatingLexiconForm_ || !hLexiconList_) {
        return;
    }
    const int row = ListView_GetNextItem(hLexiconList_, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<std::size_t>(row) >= lexiconRows_.size()) {
        clear_lexicon_entry_form();
        return;
    }

    const auto& selected = lexiconRows_[static_cast<std::size_t>(row)];
    selectedLexiconText_ = utf8_to_wstr(selected.text);
    selectedLexiconCode_ = utf8_to_wstr(selected.code);
    selectedLexiconHasSystem_ = selected.has_system_source;
    selectedLexiconHasUser_ = selected.has_user_source;
    selectedLexiconSystemDisabled_ = selected.system_disabled;
    if (current_lexicon_resource() == LexiconResource::kUserLexicon) {
        updatingLexiconForm_ = true;
        SetWindowTextW(hLexiconText_, selectedLexiconText_.c_str());
        SendMessageW(hLexiconCode_, CB_RESETCONTENT, 0, 0);
        SetWindowTextW(hLexiconCode_, selectedLexiconCode_.c_str());
        updatingLexiconForm_ = false;
        lexiconCodeManuallyEdited_ = false;
    }
    update_lexicon_entry_actions();
}

void EditorApp::update_lexicon_entry_actions() {
    const bool lexicon_page = current_lexicon_resource() == LexiconResource::kUserLexicon;
    const bool mutation_available = lexiconServerAvailable_ && !lexiconImportRunning_;
    const bool has_text = hLexiconText_ && GetWindowTextLengthW(hLexiconText_) > 0;
    const bool has_code = hLexiconCode_ && GetWindowTextLengthW(hLexiconCode_) > 0;
    EnableWindow(hLexiconAdd_, lexicon_page && mutation_available && has_text && has_code &&
                                   !selectedLexiconHasUser_);
    EnableWindow(hLexiconSave_, lexicon_page && mutation_available && has_text && has_code &&
                                   selectedLexiconHasUser_);
    EnableWindow(hLexiconDelete_, lexicon_page && mutation_available && selectedLexiconHasUser_);
    EnableWindow(hLexiconPreferenceDelete_,
                 !lexicon_page && mutation_available && selectedLexiconHasUser_);
    EnableWindow(hLexiconSystemAction_, lexicon_page && mutation_available &&
                                            lexiconDisabledStateAvailable_ &&
                                            selectedLexiconHasSystem_);
    SetWindowTextW(hLexiconSystemAction_,
                   selectedLexiconSystemDisabled_ ? L"恢复系统词" : L"隐藏系统词");
    EnableWindow(hLexiconImport_, lexicon_page && mutation_available);
    EnableWindow(hLexiconExport_, lexicon_page && mutation_available);

    const int lexicon_visibility = lexicon_page ? SW_SHOW : SW_HIDE;
    const int preference_visibility = lexicon_page ? SW_HIDE : SW_SHOW;
    for (HWND control :
         {hLexiconTextLabel_, hLexiconText_, hLexiconCodeLabel_, hLexiconCode_, hLexiconAdd_,
          hLexiconSave_, hLexiconDelete_, hLexiconSystemAction_, hLexiconImport_, hLexiconExport_}) {
        ShowWindow(control, lexicon_visibility);
    }
    ShowWindow(hLexiconPreferenceDelete_, preference_visibility);
    ShowWindow(hLexiconPreferenceClear_, preference_visibility);
    EnableWindow(hLexiconPreferenceClear_, !lexicon_page && mutation_available);
    ShowWindow(hLexiconLearningNotice_, preference_visibility);
    if (!lexicon_page) {
        SetWindowTextW(hLexiconLearningNotice_, config_.candidate_learning
                                                    ? L"选词偏好仅用于本机候选排序。"
                                                    : L"已经停止记录偏好；已有偏好不会参与排序。");
    }

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT;
    column.pszText = const_cast<LPWSTR>(lexicon_page ? L"来源" : L"选择次数");
    ListView_SetColumn(hLexiconList_, 2, &column);
    column.pszText = const_cast<LPWSTR>(lexicon_page ? L"状态" : L"");
    ListView_SetColumn(hLexiconList_, 3, &column);
}

} // namespace settings
} // namespace cxxime
