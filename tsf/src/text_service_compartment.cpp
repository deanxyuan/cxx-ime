// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"

#include <cxxime/logging.h>

#include "tsf_imm_mode.h"

namespace {

class ScopedFlag {
public:
    explicit ScopedFlag(bool& flag) : flag_(flag), previous_(flag) {
        flag_ = true;
    }

    ~ScopedFlag() {
        flag_ = previous_;
    }

private:
    bool& flag_;
    bool previous_;
};

} // namespace

void TextService::_register_conversion_compartment_sink() {
    _unregister_conversion_compartment_sink();

    ITfCompartmentMgr* compartment_manager = nullptr;
    const HRESULT manager_hr = _threadMgr
        ? _threadMgr->QueryInterface(
              IID_ITfCompartmentMgr,
              reinterpret_cast<void**>(&compartment_manager))
        : E_POINTER;

    HRESULT compartment_hr = E_UNEXPECTED;
    if (SUCCEEDED(manager_hr) && compartment_manager) {
        compartment_hr = compartment_manager->GetCompartment(
            GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
            &_conversionCompartment);
        compartment_manager->Release();
    }

    HRESULT source_hr = E_UNEXPECTED;
    if (SUCCEEDED(compartment_hr) && _conversionCompartment) {
        source_hr = _conversionCompartment->QueryInterface(
            IID_ITfSource,
            reinterpret_cast<void**>(&_conversionCompartmentSource));
    }

    HRESULT advise_hr = E_UNEXPECTED;
    if (SUCCEEDED(source_hr) && _conversionCompartmentSource) {
        advise_hr = _conversionCompartmentSource->AdviseSink(
            IID_ITfCompartmentEventSink,
            static_cast<ITfCompartmentEventSink*>(this),
            &_dwConversionCompartmentCookie);
    }

    cxxime_tsf::trace_conversion_sink_lifecycle(
        "advise", manager_hr, compartment_hr, source_hr, advise_hr,
        _dwConversionCompartmentCookie);
    if (FAILED(advise_hr)) {
        CXXIME_LOG(L"Conversion compartment sink registration failed: hr=0x%08x",
                   advise_hr);
        _unregister_conversion_compartment_sink();
    }
}

void TextService::_unregister_conversion_compartment_sink() {
    const DWORD cookie = _dwConversionCompartmentCookie;
    HRESULT unadvise_hr = S_FALSE;
    if (_conversionCompartmentSource && cookie != TF_INVALID_COOKIE) {
        unadvise_hr = _conversionCompartmentSource->UnadviseSink(cookie);
    }
    if (_conversionCompartmentSource ||
        _conversionCompartment ||
        cookie != TF_INVALID_COOKIE) {
        cxxime_tsf::trace_conversion_sink_lifecycle(
            "unadvise", S_OK, S_OK, S_OK, unadvise_hr, cookie);
    }

    _dwConversionCompartmentCookie = TF_INVALID_COOKIE;
    if (_conversionCompartmentSource) {
        _conversionCompartmentSource->Release();
        _conversionCompartmentSource = nullptr;
    }
    if (_conversionCompartment) {
        _conversionCompartment->Release();
        _conversionCompartment = nullptr;
    }
}

HRESULT TextService::_read_conversion_mode_compartment(
    DWORD* conversion_mode,
    VARTYPE* value_type) const {
    if (!conversion_mode || !value_type) {
        return E_INVALIDARG;
    }
    *conversion_mode = 0;
    *value_type = VT_EMPTY;
    if (!_conversionCompartment) {
        return E_POINTER;
    }

    VARIANT value = {};
    VariantInit(&value);
    const HRESULT value_hr = _conversionCompartment->GetValue(&value);
    *value_type = value.vt;
    HRESULT result = value_hr;
    if (SUCCEEDED(value_hr)) {
        if (value.vt == VT_I4 || value.vt == VT_INT) {
            *conversion_mode = static_cast<DWORD>(value.lVal);
        } else if (value.vt == VT_UI4 || value.vt == VT_UINT) {
            *conversion_mode = value.ulVal;
        } else {
            result = DISP_E_TYPEMISMATCH;
        }
    }
    VariantClear(&value);
    return result;
}

STDMETHODIMP TextService::OnChange(REFGUID rguid) {
    if (!IsEqualGUID(
            rguid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
        return S_OK;
    }

    DWORD conversion_mode = 0;
    VARTYPE value_type = VT_EMPTY;
    const HRESULT value_hr = _read_conversion_mode_compartment(
        &conversion_mode, &value_type);
    const bool before_chinese = _chinese_mode;
    const bool requested_chinese =
        (conversion_mode & TF_CONVERSIONMODE_NATIVE) != 0;
    const bool self_write = _writingConversionCompartment;
    const bool was_composing = _composing;
    ScopedFlag handling_change(_handlingConversionCompartmentChange);

    cxxime_tsf::TraceConversionSinkChange trace_event;
    trace_event.value_hr = value_hr;
    trace_event.value_type = static_cast<uint16_t>(value_type);
    trace_event.conversion_mode = conversion_mode;
    trace_event.self_write = self_write;
    trace_event.composing = was_composing;
    trace_event.before_chinese = before_chinese;
    trace_event.requested_chinese = requested_chinese;

    auto trace_change = [&](bool set_attempted,
                             bool set_succeeded,
                             bool commit_requested,
                             uint32_t commit_text_length,
                             const char* result) {
        trace_event.set_attempted = set_attempted;
        trace_event.set_succeeded = set_succeeded;
        trace_event.commit_requested = commit_requested;
        trace_event.commit_text_length = commit_text_length;
        trace_event.after_chinese = _chinese_mode;
        trace_event.result = result;
        cxxime_tsf::trace_conversion_sink_change(trace_event);
    };

    if (FAILED(value_hr)) {
        trace_change(false, false, false, 0, "read_failed");
        return S_OK;
    }
    if (self_write) {
        trace_change(false, false, false, 0, "self_write");
        return S_OK;
    }
    if (!_activated) {
        trace_change(false, false, false, 0, "inactive");
        return S_OK;
    }
    if (before_chinese == requested_chinese) {
        trace_change(false, false, false, 0, "already_aligned");
        return S_OK;
    }

    {
        std::lock_guard<std::mutex> lock(_lastImeStatusMutex);
        if (_hasLastImeStatus) {
            trace_event.status_details = true;
            trace_event.before_full_shape = _lastImeStatus.full_shape();
            trace_event.before_chinese_punct = _lastImeStatus.chinese_punct();
            trace_event.before_input_mode =
                static_cast<uint32_t>(_lastImeStatus.input_mode);
        }
    }

    cxxime::IPCResponse response = {};
    const bool set_succeeded =
        _ensure_ipc_session() &&
        _client.set_chinese_mode(_sessionId, requested_chinese, response) &&
        response.status == cxxime::IPCStatus::OK;
    trace_event.ipc_us = _client.last_ipc_us();
    if (!set_succeeded) {
        trace_change(true, false, false, 0, "set_failed");
        return S_OK;
    }

    trace_event.after_full_shape = response.ime_status.full_shape();
    trace_event.after_chinese_punct = response.ime_status.chinese_punct();
    trace_event.after_input_mode =
        static_cast<uint32_t>(response.ime_status.input_mode);
    _sync_ime_status(response.ime_status);

    bool commit_requested = false;
    uint32_t commit_text_length = 0;
    if (response.commit_text[0] != '\0') {
        const std::wstring commit_text = utf8_to_wstring(response.commit_text);
        if (!commit_text.empty()) {
            ITfContext* context = _current_edit_context_for_composition();
            if (context) {
                _commit_text(context, commit_text, false);
                context->Release();
            } else {
                insert_text(commit_text, false);
            }
            commit_requested = true;
            commit_text_length = static_cast<uint32_t>(commit_text.length());
        }
    }

    if ((was_composing || commit_requested) && !response.composing) {
        if (!commit_requested) {
            ITfContext* context = _current_edit_context_for_composition();
            if (context) {
                _end_composition(context);
                context->Release();
            }
        }
        _composing = false;
        _lastInlineCompositionText.clear();
        _hide_candidate_window("hide:conversion_change_commit");
        _end_reading_ui_element("hide:conversion_change_commit_reading");
        _reset_trace_composition("conversion_change_commit");
    }

    trace_change(true, true, commit_requested, commit_text_length,
                 _chinese_mode == requested_chinese ? "applied" : "caps_lock");
    return S_OK;
}
