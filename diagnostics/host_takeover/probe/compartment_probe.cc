// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <new>

#include <ctffunc.h>

#include <cxxime/host_trace.h>

namespace cxxime_probe {

class ConversionCompartmentProbe final : public ITfCompartmentEventSink {
public:
    bool initialize(ITfThreadMgr* thread_mgr, TfClientId client_id) {
        client_id_ = client_id;
        ITfCompartmentMgr* manager = nullptr;
        const HRESULT manager_hr = thread_mgr
            ? thread_mgr->QueryInterface(
                  IID_ITfCompartmentMgr, reinterpret_cast<void**>(&manager))
            : E_POINTER;
        HRESULT compartment_hr = E_UNEXPECTED;
        if (SUCCEEDED(manager_hr) && manager) {
            compartment_hr = manager->GetCompartment(
                GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &compartment_);
            manager->Release();
        }

        HRESULT source_hr = E_UNEXPECTED;
        if (SUCCEEDED(compartment_hr) && compartment_) {
            source_hr = compartment_->QueryInterface(
                IID_ITfSource, reinterpret_cast<void**>(&source_));
        }
        HRESULT advise_hr = E_UNEXPECTED;
        if (SUCCEEDED(source_hr) && source_) {
            advise_hr = source_->AdviseSink(
                IID_ITfCompartmentEventSink,
                static_cast<ITfCompartmentEventSink*>(this), &cookie_);
        }

        cxxime::write_host_trace("probe", "probe.conversion_subscription", {
            {"manager_hr", static_cast<int64_t>(manager_hr)},
            {"compartment_hr", static_cast<int64_t>(compartment_hr)},
            {"source_hr", static_cast<int64_t>(source_hr)},
            {"advise_hr", static_cast<int64_t>(advise_hr)},
            {"cookie", cookie_},
            {"result", SUCCEEDED(advise_hr) ? "subscribed" : "failed"},
        });
        trace_value("initialize");
        return SUCCEEDED(advise_hr);
    }

    void shutdown() {
        if (source_ && cookie_ != TF_INVALID_COOKIE) {
            source_->UnadviseSink(cookie_);
            cookie_ = TF_INVALID_COOKIE;
        }
        if (source_) {
            source_->Release();
            source_ = nullptr;
        }
        if (compartment_) {
            compartment_->Release();
            compartment_ = nullptr;
        }
    }

    void toggle() {
        DWORD current = 0;
        const HRESULT read_hr = read_value(&current);
        if (FAILED(read_hr)) {
            cxxime::write_host_trace("probe", "probe.conversion_write", {
                {"read_hr", static_cast<int64_t>(read_hr)},
                {"result", "read_failed"},
            });
            return;
        }

        constexpr DWORD kChineseMode =
            TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_SYMBOL;
        const DWORD requested = (current & TF_CONVERSIONMODE_NATIVE) != 0
            ? current & ~kChineseMode
            : current | kChineseMode;
        VARIANT value = {};
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = static_cast<LONG>(requested);
        const HRESULT write_hr = compartment_->SetValue(client_id_, &value);
        VariantClear(&value);
        cxxime::write_host_trace("probe", "probe.conversion_write", {
            {"read_hr", static_cast<int64_t>(read_hr)},
            {"previous_mode", current},
            {"requested_mode", requested},
            {"write_hr", static_cast<int64_t>(write_hr)},
            {"result", SUCCEEDED(write_hr) ? "written" : "failed"},
        });
        trace_value("write_complete");
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_INVALIDARG;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_ITfCompartmentEventSink)) {
            *object = static_cast<ITfCompartmentEventSink*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    STDMETHODIMP OnChange(REFGUID compartment) override {
        const bool expected = IsEqualGUID(
            compartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
        cxxime::write_host_trace("probe", "probe.conversion_change", {
            {"compartment", cxxime::host_trace_guid(compartment)},
            {"expected_compartment", expected},
            {"result", expected ? "notified" : "unexpected"},
        });
        trace_value("change_notification");
        return S_OK;
    }

private:
    HRESULT read_value(DWORD* conversion_mode) const {
        if (!conversion_mode) {
            return E_INVALIDARG;
        }
        *conversion_mode = 0;
        if (!compartment_) {
            return E_POINTER;
        }

        VARIANT value = {};
        VariantInit(&value);
        const HRESULT result = compartment_->GetValue(&value);
        if (SUCCEEDED(result)) {
            if (value.vt == VT_I4 || value.vt == VT_INT) {
                *conversion_mode = static_cast<DWORD>(value.lVal);
            } else if (value.vt == VT_UI4 || value.vt == VT_UINT) {
                *conversion_mode = value.ulVal;
            } else {
                VariantClear(&value);
                return DISP_E_TYPEMISMATCH;
            }
        }
        VariantClear(&value);
        return result;
    }

    void trace_value(const char* trigger) const {
        DWORD conversion_mode = 0;
        const HRESULT value_hr = read_value(&conversion_mode);
        cxxime::write_host_trace("probe", "probe.conversion_compartment", {
            {"trigger", trigger ? trigger : ""},
            {"value_hr", static_cast<int64_t>(value_hr)},
            {"conversion_mode", conversion_mode},
            {"native", (conversion_mode & TF_CONVERSIONMODE_NATIVE) != 0},
            {"symbol", (conversion_mode & TF_CONVERSIONMODE_SYMBOL) != 0},
            {"full_shape", (conversion_mode & TF_CONVERSIONMODE_FULLSHAPE) != 0},
            {"result", SUCCEEDED(value_hr) ? "read" : "failed"},
        });
    }

    LONG ref_count_ = 1;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    ITfCompartment* compartment_ = nullptr;
    ITfSource* source_ = nullptr;
    DWORD cookie_ = TF_INVALID_COOKIE;
};

void ProbeApp::initialize_conversion_compartment_probe() {
    conversion_compartment_probe_ = new (std::nothrow) ConversionCompartmentProbe();
    if (!conversion_compartment_probe_) {
        cxxime::write_host_trace("probe", "probe.conversion_subscription", {
            {"result", "allocation_failed"},
        });
        return;
    }
    conversion_compartment_probe_->initialize(thread_mgr_, client_id_);
}

void ProbeApp::shutdown_conversion_compartment_probe() {
    if (!conversion_compartment_probe_) {
        return;
    }
    conversion_compartment_probe_->shutdown();
    conversion_compartment_probe_->Release();
    conversion_compartment_probe_ = nullptr;
}

void ProbeApp::toggle_conversion_compartment() {
    if (conversion_compartment_probe_) {
        conversion_compartment_probe_->toggle();
    }
}

} // namespace cxxime_probe
