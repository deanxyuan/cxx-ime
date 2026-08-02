// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <new>

#include <cxxime/logging.h>
#include <cxxime/tsf_factory.h>

#include "about_dialog.h"
#include "config_coordinator.h"
#include "display_attribute.h"
#include "globals.h"
#include "host_compatibility/host_classification_compatibility.h"
#include "language_bar.h"
#include "settings_launcher.h"
#include "tsf_activation.h"
#include "tsf_imm_mode.h"
#include "tsf_ui_element_observer.h"

void TextService::_handle_ime_menu_command(cxxime::ImeMenuCommand command) {
    if (command == cxxime::ImeMenuCommand::kPinyin ||
        command == cxxime::ImeMenuCommand::kWubi ||
        command == cxxime::ImeMenuCommand::kMixed) {
        cxxime::InputMode mode = cxxime::InputMode::PINYIN;
        if (command == cxxime::ImeMenuCommand::kWubi) {
            mode = cxxime::InputMode::WUBI;
        } else if (command == cxxime::ImeMenuCommand::kMixed) {
            mode = cxxime::InputMode::MIXED;
        }

        CXXIME_LOG(L"menu_command: input_mode=%d, sessionId=%u",
                   static_cast<int>(mode), _sessionId);
        cxxime::IPCResponse response = {};
        if (_ensure_ipc_session()) {
            _client.switch_input_mode(_sessionId, mode, response);
        }
        if (response.status == cxxime::IPCStatus::OK) {
            _sync_ime_status(response.ime_status);
        }
        return;
    }

    switch (command) {
    case cxxime::ImeMenuCommand::kDictionary:
        cxxime_tsf::open_settings(cxxime::SettingsPanel::kDictionary);
        break;
    case cxxime::ImeMenuCommand::kToggleStatusWindow: {
        bool enabled = !_config.status_window.enable;
        cxxime_tsf::set_status_window_enabled(enabled);
        break;
    }
    case cxxime::ImeMenuCommand::kSettings:
        cxxime_tsf::open_settings();
        break;
    case cxxime::ImeMenuCommand::kAbout:
        show_about_dialog();
        break;
    case cxxime::ImeMenuCommand::kPinyin:
    case cxxime::ImeMenuCommand::kWubi:
    case cxxime::ImeMenuCommand::kMixed:
        break;
    }
}

void TextService::_start_host_compatibility_runtime() {
    if (_hostCompatibilityRuntimeActive) {
        return;
    }

    cxxime_tsf::activate_host_classification_compatibility();
    cxxime_tsf::start_stage_runtime(
        cxxime_tsf::host_classification_compatibility_snapshot());
    _hostCompatibilityRuntimeActive = true;
}

void TextService::_stop_host_compatibility_runtime() {
    if (!_hostCompatibilityRuntimeActive) {
        return;
    }

    const cxxime_tsf::HostClassificationCompatibilitySnapshot snapshot =
        cxxime_tsf::deactivate_host_classification_compatibility();
    cxxime_tsf::stop_stage_runtime(snapshot);
    _hostCompatibilityRuntimeActive = false;
}

void TextService::_sync_conversion_mode_compartment(
        const cxxime::ImeStatus& status) {
    if (!_threadMgr || _clientId == TF_CLIENTID_NULL) {
        return;
    }

    ITfCompartmentMgr* compartment_mgr = nullptr;
    HRESULT hr = _threadMgr->QueryInterface(
        IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartment_mgr));
    if (FAILED(hr) || !compartment_mgr) {
        return;
    }

    ITfCompartment* compartment = nullptr;
    hr = compartment_mgr->GetCompartment(
        GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &compartment);
    compartment_mgr->Release();
    if (FAILED(hr) || !compartment) {
        return;
    }

    DWORD conversion_mode = 0;
    VARIANT current = {};
    VariantInit(&current);
    const HRESULT get_value_hr = compartment->GetValue(&current);
    if (SUCCEEDED(get_value_hr)) {
        if (current.vt == VT_I4 || current.vt == VT_INT) {
            conversion_mode = static_cast<DWORD>(current.lVal);
        } else if (current.vt == VT_UI4 || current.vt == VT_UINT) {
            conversion_mode = current.ulVal;
        }
    }
    VariantClear(&current);

    constexpr DWORD kChineseMode =
        TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_SYMBOL;
    DWORD requested_mode = conversion_mode;
    if (status.chinese_mode) {
        requested_mode |= kChineseMode;
    } else {
        requested_mode &= ~kChineseMode;
    }

    const bool set_attempted = requested_mode != conversion_mode;
    HRESULT set_value_hr = S_FALSE;
    if (set_attempted) {
        VARIANT next = {};
        VariantInit(&next);
        next.vt = VT_I4;
        next.lVal = static_cast<LONG>(requested_mode);
        _writingConversionCompartment = true;
        set_value_hr = compartment->SetValue(_clientId, &next);
        _writingConversionCompartment = false;
        CXXIME_LOG(L"sync_conversion_mode: chinese=%d, mode=0x%08x->0x%08x, hr=0x%08x",
                   status.chinese_mode ? 1 : 0, conversion_mode,
                   requested_mode, set_value_hr);
        VariantClear(&next);
    }
    cxxime_tsf::trace_stage_conversion_compartment(
        status.chinese_mode, get_value_hr, conversion_mode, requested_mode,
        set_attempted, set_value_hr);
    compartment->Release();
}

void TextService::_register_thread_sinks() {
    ITfSource* source = nullptr;
    const HRESULT source_hr = _threadMgr
        ? _threadMgr->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))
        : E_POINTER;
    HRESULT thread_focus_hr = E_NOINTERFACE;
    HRESULT thread_mgr_hr = E_NOINTERFACE;
    bool thread_focus_attempted = false;
    bool thread_mgr_attempted = false;
    if (SUCCEEDED(source_hr) && source) {
        thread_focus_attempted = true;
        thread_focus_hr = source->AdviseSink(
            IID_ITfThreadFocusSink,
            static_cast<ITfThreadFocusSink*>(this), &_dwThreadFocusCookie);
        thread_mgr_attempted = true;
        thread_mgr_hr = source->AdviseSink(
            IID_ITfThreadMgrEventSink,
            static_cast<ITfThreadMgrEventSink*>(this), &_dwThreadMgrEventCookie);
        source->Release();
    }
    cxxime_tsf::trace_stage_thread_sinks(
        "advise", source_hr,
        thread_focus_attempted, thread_focus_hr, _dwThreadFocusCookie,
        thread_mgr_attempted, thread_mgr_hr, _dwThreadMgrEventCookie);
    cxxime_tsf::start_stage_ui_element_observer(_threadMgr, _activateFlags);
}

void TextService::_unregister_thread_sinks() {
    if (!_threadMgr) {
        _dwThreadFocusCookie = TF_INVALID_COOKIE;
        _dwThreadMgrEventCookie = TF_INVALID_COOKIE;
        return;
    }

    ITfSource* source = nullptr;
    const DWORD thread_focus_cookie = _dwThreadFocusCookie;
    const DWORD thread_mgr_cookie = _dwThreadMgrEventCookie;
    const HRESULT source_hr = _threadMgr->QueryInterface(
        IID_ITfSource, reinterpret_cast<void**>(&source));
    HRESULT thread_focus_hr = E_NOINTERFACE;
    HRESULT thread_mgr_hr = E_NOINTERFACE;
    bool thread_focus_attempted = false;
    bool thread_mgr_attempted = false;
    if (SUCCEEDED(source_hr) && source) {
        if (thread_focus_cookie != TF_INVALID_COOKIE) {
            thread_focus_attempted = true;
            thread_focus_hr = source->UnadviseSink(thread_focus_cookie);
        }
        if (thread_mgr_cookie != TF_INVALID_COOKIE) {
            thread_mgr_attempted = true;
            thread_mgr_hr = source->UnadviseSink(thread_mgr_cookie);
        }
        source->Release();
    }
    cxxime_tsf::trace_stage_thread_sinks(
        "unadvise", source_hr,
        thread_focus_attempted, thread_focus_hr, thread_focus_cookie,
        thread_mgr_attempted, thread_mgr_hr, thread_mgr_cookie);
    _dwThreadFocusCookie = TF_INVALID_COOKIE;
    _dwThreadMgrEventCookie = TF_INVALID_COOKIE;
}

HRESULT TextService::_register_key_event_sink() {
    if (!_threadMgr)
        return E_FAIL;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    if (FAILED(_threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
        return E_FAIL;

    HRESULT hr = pKeystrokeMgr->AdviseKeyEventSink(_clientId, static_cast<ITfKeyEventSink*>(this), TRUE);
    pKeystrokeMgr->Release();
    return hr;
}

HRESULT TextService::_unregister_key_event_sink() {
    if (!_threadMgr)
        return E_FAIL;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    if (FAILED(_threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
        return E_FAIL;

    HRESULT hr = pKeystrokeMgr->UnadviseKeyEventSink(_clientId);
    pKeystrokeMgr->Release();
    return hr;
}

HRESULT TextService::_register_preserved_key() {
    if (!_threadMgr)
        return E_FAIL;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    if (FAILED(_threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
        return E_FAIL;

    // Register Ctrl+Space as preserved key for mode toggle
    TF_PRESERVEDKEY prekey = {};
    prekey.uVKey = VK_SPACE;
    prekey.uModifiers = TF_MOD_CONTROL;
    HRESULT hr = pKeystrokeMgr->PreserveKey(
        _clientId,
        c_guidPreservedKey_Toggle,
        &prekey,
        L"Toggle Chinese/English",
        (ULONG)wcslen(L"Toggle Chinese/English"));

    pKeystrokeMgr->Release();
    return hr;
}

HRESULT TextService::_unregister_preserved_key() {
    if (!_threadMgr)
        return E_FAIL;

    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    if (FAILED(_threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
        return E_FAIL;

    HRESULT hr = pKeystrokeMgr->UnpreserveKey(c_guidPreservedKey_Toggle, nullptr);
    pKeystrokeMgr->Release();
    return hr;
}

bool TextService::_register_display_attribute_atom() {
    ITfCategoryMgr* category_mgr = nullptr;
    HRESULT hr = E_UNEXPECTED;
    if ((_activateFlags & TF_TMAE_COMLESS) != 0) {
        hr = cxxime::create_tsf_category_manager_without_com(&category_mgr);
    } else {
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ITfCategoryMgr, reinterpret_cast<void**>(&category_mgr));
    }
    if (FAILED(hr) || !category_mgr)
        return false;

    hr = category_mgr->RegisterGUID(c_guidDisplayAttribute, &_displayAttributeAtom);
    category_mgr->Release();
    if (FAILED(hr)) {
        _displayAttributeAtom = 0;
        CXXIME_LOG(L"Register display attribute atom failed: hr=0x%08x", hr);
        return false;
    }

    return true;
}

STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum)
        return E_INVALIDARG;
    auto* pEnum = new (std::nothrow) ::EnumDisplayAttributeInfo();
    *ppEnum = pEnum;
    return pEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID rguid,
                                                  ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo)
        return E_INVALIDARG;
    *ppInfo = nullptr;

    if (IsEqualGUID(rguid, c_guidDisplayAttribute)) {
        auto* pInfo = new (std::nothrow) ::DisplayAttributeInfo();
        *ppInfo = pInfo;
        return pInfo ? S_OK : E_OUTOFMEMORY;
    }
    return E_INVALIDARG;
}
