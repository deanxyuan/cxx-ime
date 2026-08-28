// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "editor_app.h"

#include <commctrl.h>

#include "editor_app_internal.h"
#include "lexicon_query_service.h"

namespace cxxime {
namespace settings {
namespace {

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

void set_list_column(HWND list, int index, const wchar_t* text, int width) {
    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPWSTR>(text);
    column.cx = width;
    column.iSubItem = index;
    if (ListView_GetColumnWidth(list, index) == 0) {
        ListView_InsertColumn(list, index, &column);
    } else {
        ListView_SetColumn(list, index, &column);
    }
}

} // namespace

void EditorApp::create_dictionary_panel(HWND panel, int panel_width) {
    const int top = kPanelPadTop;
    RECT panel_rect = {};
    GetClientRect(panel, &panel_rect);
    SetWindowSubclass(panel, PanelForwardProc, 4000, reinterpret_cast<DWORD_PTR>(hwnd_));
    lexiconQueryService_ = std::make_shared<LexiconQueryService>();

    lexiconViewTabs_ = create_lexicon_view_tabs(panel, 4014, 4019, 4018, kPanelPadLeft, top,
                                                S(270), kCtrlH, get_font());

    const int content_right = panel_width - S(10);
    const int query_y = top + kRowH;
    const int kind_width = S(100);
    const int query_button_width = S(68);
    const int control_gap = S(8);
    const int query_edit_x = kPanelPadLeft + kind_width + S(12);
    const int refresh_button_x = content_right - query_button_width;
    const int query_button_x = refresh_button_x - control_gap - query_button_width;
    const int query_edit_width = query_button_x - control_gap - query_edit_x;

    hLexiconKind_ = make_combo(4013, kPanelPadLeft, query_y, kind_width, panel);
    combo_add(hLexiconKind_, L"拼音");
    combo_add(hLexiconKind_, L"五笔");
    combo_sel(hLexiconKind_, L"拼音");
    set_combo_drop_count(hLexiconKind_, 2);
    hLexiconQuery_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        query_edit_x, query_y, query_edit_width, kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4000)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconQuery_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    SendMessageW(hLexiconQuery_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"词语或编码"));
    SetWindowSubclass(hLexiconQuery_, QueryEditProc, 4000, reinterpret_cast<DWORD_PTR>(hwnd_));

    auto make_button = [&](int id, const wchar_t* text, int x, int y, int width) {
        HWND button = CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y, width,
            kCtrlH, panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandle(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
        return button;
    };
    make_button(4001, L"查询", query_button_x, query_y, query_button_width);
    make_button(4002, L"刷新", refresh_button_x, query_y, query_button_width);

    const int list_y = query_y + kRowH;
    const int status_y = panel_rect.bottom - kCtrlH - S(4);
    const int file_y = status_y - kRowH;
    const int action_y = file_y - kRowH;
    const int edit_y = action_y - kRowH;
    const int list_width = panel_width - kPanelPadLeft - S(10);
    const int list_height = edit_y - list_y - S(8);
    hLexiconList_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        kPanelPadLeft, list_y, list_width, list_height, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4003)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconList_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    ListView_SetExtendedListViewStyle(hLexiconList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    set_list_column(hLexiconList_, 0, L"词语", S(170));
    set_list_column(hLexiconList_, 1, L"编码", S(145));
    set_list_column(hLexiconList_, 2, L"来源", S(105));
    set_list_column(hLexiconList_, 3, L"状态", list_width - S(420));

    hLexiconTextLabel_ = CreateWindowExW(0, L"STATIC", L"词语:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         kPanelPadLeft, edit_y + S(3), S(42), kCtrlH, panel,
                                         nullptr, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconTextLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    const int text_x = kPanelPadLeft + S(42);
    hLexiconText_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        text_x, edit_y, S(190), kCtrlH, panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4004)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconText_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    const int code_x = text_x + S(204);
    hLexiconCodeLabel_ = CreateWindowExW(0, L"STATIC", L"编码:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         code_x, edit_y + S(3), S(42), kCtrlH, panel, nullptr,
                                         GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconCodeLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    hLexiconCode_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        code_x + S(42), edit_y, S(155), S(180), panel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4005)), GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconCode_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    hLexiconAdd_ = make_button(4006, L"新增", kPanelPadLeft, action_y, S(64));
    hLexiconSave_ = make_button(4007, L"保存修改", kPanelPadLeft + S(72), action_y, S(86));
    hLexiconDelete_ = make_button(4008, L"删除用户词", kPanelPadLeft + S(166), action_y, S(86));
    hLexiconSystemAction_ =
        make_button(4016, L"隐藏系统词", kPanelPadLeft + S(260), action_y, S(86));
    hLexiconPreferenceDelete_ =
        make_button(4017, L"删除选中", kPanelPadLeft, action_y, S(86));
    hLexiconPreferenceClear_ =
        make_button(4015, L"清空全部偏好", kPanelPadLeft + S(94), action_y, S(112));
    ShowWindow(hLexiconPreferenceDelete_, SW_HIDE);
    ShowWindow(hLexiconPreferenceClear_, SW_HIDE);
    hCandidateOrderFirst_ = make_button(4020, L"固定到首位", kPanelPadLeft, action_y, S(88));
    hCandidateOrderAppend_ =
        make_button(4025, L"追加固定", kPanelPadLeft + S(94), action_y, S(72));
    hCandidateOrderUp_ =
        make_button(4021, L"固定上移", kPanelPadLeft + S(172), action_y, S(70));
    hCandidateOrderDown_ =
        make_button(4022, L"固定下移", kPanelPadLeft + S(248), action_y, S(70));
    hCandidateOrderUnpin_ =
        make_button(4023, L"取消固定", kPanelPadLeft + S(324), action_y, S(72));
    hCandidateOrderReset_ =
        make_button(4024, L"恢复默认", kPanelPadLeft + S(402), action_y, S(78));
    for (HWND control : {hCandidateOrderFirst_, hCandidateOrderAppend_, hCandidateOrderUp_,
                         hCandidateOrderDown_, hCandidateOrderUnpin_, hCandidateOrderReset_}) {
        ShowWindow(control, SW_HIDE);
    }

    hLexiconOpenDirectory_ = make_button(4012, L"打开目录", kPanelPadLeft, file_y, S(86));
    hLexiconImport_ = make_button(4010, L"导入", kPanelPadLeft + S(94), file_y, S(64));
    hLexiconExport_ = make_button(4011, L"导出", kPanelPadLeft + S(166), file_y, S(64));
    hLexiconLearningNotice_ =
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, kPanelPadLeft, edit_y + S(3),
                        panel_width - kPanelPadLeft - S(10), kCtrlH, panel, nullptr,
                        GetModuleHandle(nullptr), nullptr);
    SendMessageW(hLexiconLearningNotice_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);

    hLexiconStatus_ = CreateWindowExW(
        0, L"STATIC", L"输入词语或编码开始查询",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, kPanelPadLeft, status_y,
        panel_width - kPanelPadLeft - S(10), kCtrlH, panel, nullptr, GetModuleHandle(nullptr),
        nullptr);
    SendMessageW(hLexiconStatus_, WM_SETFONT, reinterpret_cast<WPARAM>(get_font()), TRUE);
    update_lexicon_entry_actions();
}

bool EditorApp::handle_dictionary_command(int control_id, int notification) {
    switch (control_id) {
    case 4001:
        query_lexicon_entries(false);
        return true;
    case 4002:
        refresh_lexicon_entries();
        return true;
    case 4004:
        if (notification == EN_CHANGE && !updatingLexiconForm_) {
            lexiconCodeManuallyEdited_ = false;
            SetTimer(hwnd_, kLexiconCodeTimerId, 150, nullptr);
            update_lexicon_entry_actions();
        }
        return true;
    case 4005:
        if (current_lexicon_resource() == LexiconResource::kManualCandidateOrder) {
            if (notification == CBN_SELCHANGE && !updatingLexiconForm_) {
                query_lexicon_entries(false);
            }
            return true;
        }
        if ((notification == CBN_EDITCHANGE || notification == CBN_SELCHANGE) &&
            !updatingLexiconForm_) {
            lexiconCodeManuallyEdited_ = true;
            update_lexicon_entry_actions();
        }
        return true;
    case 4006:
        add_lexicon_entry();
        return true;
    case 4007:
        save_lexicon_entry();
        return true;
    case 4008:
        delete_lexicon_entries();
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
            clear_lexicon_entry_form();
            query_lexicon_entries(false);
        }
        return true;
    case 4014:
    case 4019:
    case 4018: {
        if (notification != BN_CLICKED) {
            return true;
        }
        const LexiconResource resource = control_id == 4018
                                             ? LexiconResource::kCandidatePreference
                                             : control_id == 4019
                                                   ? LexiconResource::kManualCandidateOrder
                                                   : LexiconResource::kUserLexicon;
        if (resource == lexiconResource_) {
            return true;
        }
        lexiconResource_ = resource;
        const HWND selected = resource == LexiconResource::kCandidatePreference
                                  ? lexiconViewTabs_.preferences
                                  : resource == LexiconResource::kManualCandidateOrder
                                        ? lexiconViewTabs_.candidate_order
                                        : lexiconViewTabs_.entries;
        select_lexicon_view_tab(lexiconViewTabs_, selected);
        clear_lexicon_entry_form();
        update_lexicon_entry_actions();
        query_lexicon_entries(false);
        return true;
    }
    case 4015:
        clear_candidate_preferences();
        return true;
    case 4016:
        disable_or_restore_system_entry();
        return true;
    case 4017:
        delete_lexicon_entries();
        return true;
    case 4020:
        pin_candidate_order_first();
        return true;
    case 4025:
        append_candidate_order_pin();
        return true;
    case 4021:
        move_candidate_order(-1);
        return true;
    case 4022:
        move_candidate_order(1);
        return true;
    case 4023:
        remove_candidate_order_pin();
        return true;
    case 4024:
        reset_candidate_order();
        return true;
    default:
        return false;
    }
}

bool EditorApp::handle_dictionary_notify(LPARAM notification) {
    auto* header = reinterpret_cast<LPNMHDR>(notification);
    if (!header || header->idFrom != 4003) {
        return false;
    }
    if (header->code == LVN_ITEMCHANGED) {
        const auto* item = reinterpret_cast<LPNMLISTVIEW>(notification);
        if ((item->uChanged & LVIF_STATE) != 0) {
            on_lexicon_selection_changed();
        }
        return true;
    }
    if (header->code == LVN_KEYDOWN) {
        const auto* key = reinterpret_cast<LPNMLVKEYDOWN>(notification);
        if (key->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            select_all_lexicon_entries();
            return true;
        }
        if (key->wVKey == VK_DELETE) {
            if (current_lexicon_resource() == LexiconResource::kManualCandidateOrder) {
                remove_candidate_order_pin();
            } else {
                delete_lexicon_entries();
            }
            return true;
        }
    }
    return false;
}

UserDictKind EditorApp::current_user_dict_kind() const {
    return combo_index(hLexiconKind_) == 1 ? UserDictKind::WUBI : UserDictKind::PINYIN;
}

LexiconResource EditorApp::current_lexicon_resource() const {
    return lexiconResource_;
}

} // namespace settings
} // namespace cxxime
