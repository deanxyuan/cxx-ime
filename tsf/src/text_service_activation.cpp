// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include "host_compatibility/host_classification_compatibility.h"
#include "tsf_activation.h"
#include "tsf_host_classification.h"
#include "tsf_host_message.h"
#include "tsf_imm_mode.h"
#include "tsf_ui_element_observer.h"

#include <cxxime/logging.h>

void TextService::_start_host_takeover_runtime() {
    if (_hostTakeoverRuntimeActive) {
        return;
    }

    cxxime_tsf::activate_host_classification_compatibility();
    cxxime_tsf::trace_stage_host_classification_compatibility(
        cxxime_tsf::host_classification_compatibility_snapshot());
    cxxime_tsf::start_stage_host_message_monitor();
    _hostTakeoverRuntimeActive = true;
}

void TextService::_stop_host_takeover_runtime() {
    if (!_hostTakeoverRuntimeActive) {
        return;
    }

    const cxxime_tsf::HostClassificationCompatibilitySnapshot snapshot =
        cxxime_tsf::deactivate_host_classification_compatibility();
    cxxime_tsf::trace_stage_host_classification_compatibility(snapshot);
    cxxime_tsf::stop_stage_host_message_monitor();
    _hostTakeoverRuntimeActive = false;
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
        set_value_hr = compartment->SetValue(_clientId, &next);
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
    cxxime_tsf::start_stage_ui_element_observer(_threadMgr);
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
