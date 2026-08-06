// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "input_method_hotkey.h"

#include <string>

#include <msctf.h>
#include <objbase.h>

#include <cxxime/logging.h>
#include <cxxime/text_service_profile.h>

namespace {

constexpr int kActivateImeHotkeyIds[] = {1, 2};

} // namespace

InputMethodHotkey::~InputMethodHotkey() { shutdown(); }

bool InputMethodHotkey::initialize(HWND window, const cxxime::KeyboardShortcut& hotkey) {
    if (!window || window_) {
        return false;
    }
    window_ = window;
    return update(hotkey);
}

bool InputMethodHotkey::update(const cxxime::KeyboardShortcut& hotkey) {
    unsigned long error_code = ERROR_SUCCESS;
    if (!prepare_update(hotkey, &error_code)) {
        return false;
    }
    return commit_update();
}

bool InputMethodHotkey::prepare_update(const cxxime::KeyboardShortcut& hotkey,
                                       unsigned long* error_code) {
    cancel_update();
    if (!window_ || !cxxime::is_valid_activate_ime_shortcut(hotkey)) {
        if (error_code) {
            *error_code = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    pending_hotkey_ = hotkey;
    update_pending_ = true;
    if (hotkey == hotkey_ && (!hotkey.enabled() || registered_id_ != 0)) {
        if (error_code) {
            *error_code = ERROR_SUCCESS;
        }
        return true;
    }

    if (hotkey.enabled()) {
        pending_id_ = registered_id_ == kActivateImeHotkeyIds[0] ? kActivateImeHotkeyIds[1]
                                                                  : kActivateImeHotkeyIds[0];
        if (!register_hotkey(pending_id_, hotkey)) {
            const DWORD error = GetLastError();
            const std::string value = cxxime::keyboard_shortcut_string(hotkey);
            CXXIME_LOG(L"activate_ime_hotkey event=prepare result=0 error=%lu value=%S", error,
                       value.c_str());
            pending_hotkey_ = {};
            pending_id_ = 0;
            update_pending_ = false;
            if (error_code) {
                *error_code = error;
            }
            return false;
        }
        pending_registered_ = true;
    }

    if (error_code) {
        *error_code = ERROR_SUCCESS;
    }
    return true;
}

bool InputMethodHotkey::commit_update() {
    if (!update_pending_) {
        return false;
    }

    if (pending_hotkey_ != hotkey_ || (pending_hotkey_.enabled() && registered_id_ == 0)) {
        if (registered_id_ != 0) {
            UnregisterHotKey(window_, registered_id_);
        }
        registered_id_ = pending_registered_ ? pending_id_ : 0;
        hotkey_ = pending_hotkey_;
    }

    const std::string value = cxxime::keyboard_shortcut_string(hotkey_);
    CXXIME_LOG(L"activate_ime_hotkey event=commit result=1 value=%S", value.c_str());
    pending_hotkey_ = {};
    pending_id_ = 0;
    update_pending_ = false;
    pending_registered_ = false;
    return true;
}

void InputMethodHotkey::cancel_update() {
    if (pending_registered_ && window_) {
        UnregisterHotKey(window_, pending_id_);
    }
    pending_hotkey_ = {};
    pending_id_ = 0;
    update_pending_ = false;
    pending_registered_ = false;
}

bool InputMethodHotkey::handle(WPARAM hotkey_id) {
    if (registered_id_ == 0 || hotkey_id != static_cast<WPARAM>(registered_id_)) {
        return false;
    }
    activate_profile();
    return true;
}

void InputMethodHotkey::shutdown() {
    cancel_update();
    if (registered_id_ != 0 && window_) {
        UnregisterHotKey(window_, registered_id_);
    }
    registered_id_ = 0;
    hotkey_ = {};
    window_ = nullptr;
}

bool InputMethodHotkey::register_hotkey(int id, const cxxime::KeyboardShortcut& hotkey) {
    if (!RegisterHotKey(window_, id, cxxime::keyboard_shortcut_win32_modifiers(hotkey),
                        hotkey.virtual_key)) {
        return false;
    }
    return true;
}

bool InputMethodHotkey::activate_profile() {
    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                      reinterpret_cast<void**>(&profile_manager));
    if (SUCCEEDED(result)) {
        result = profile_manager->ActivateProfile(
            TF_PROFILETYPE_INPUTPROCESSOR, cxxime::kTextServiceLanguageId,
            cxxime::kTextServiceClsid, cxxime::kTextServiceProfileGuid, nullptr,
            TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
        profile_manager->Release();
    }
    CXXIME_LOG(L"activate_ime_hotkey event=activate result=%ld", result);
    return result == S_OK;
}
