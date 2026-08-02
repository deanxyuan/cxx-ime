// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <new>
#include <string>

#include <cxxime/stage_trace.h>
#include <cxxime/tsf_factory.h>

namespace cxxime_probe {
namespace {

class DisplayAttributeEditSession final : public ITfEditSession {
public:
    DisplayAttributeEditSession(ITfContext* context,
                                 uint64_t composition_id,
                                 DWORD element_id,
                                 const char* action,
                                 bool without_com)
        : context_(context),
          composition_id_(composition_id),
          element_id_(element_id),
          action_(action ? action : ""),
          without_com_(without_com) {
        context_->AddRef();
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_INVALIDARG;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfEditSession)) {
            *object = static_cast<ITfEditSession*>(this);
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

    STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
        ITfContextComposition* context_composition = nullptr;
        const HRESULT context_composition_hr = context_->QueryInterface(
            IID_ITfContextComposition,
            reinterpret_cast<void**>(&context_composition));

        IEnumITfCompositionView* compositions = nullptr;
        HRESULT enum_hr = E_UNEXPECTED;
        if (SUCCEEDED(context_composition_hr) && context_composition) {
            enum_hr = context_composition->EnumCompositions(&compositions);
            context_composition->Release();
        }

        ITfCompositionView* composition = nullptr;
        ULONG fetched = 0;
        HRESULT next_hr = E_UNEXPECTED;
        if (SUCCEEDED(enum_hr) && compositions) {
            next_hr = compositions->Next(1, &composition, &fetched);
            compositions->Release();
        }

        ITfRange* range = nullptr;
        HRESULT range_hr = E_UNEXPECTED;
        if (next_hr == S_OK && fetched == 1 && composition) {
            range_hr = composition->GetRange(&range);
        }
        if (composition) {
            composition->Release();
        }

        ITfProperty* property = nullptr;
        HRESULT property_hr = E_UNEXPECTED;
        if (SUCCEEDED(range_hr) && range) {
            property_hr = context_->GetProperty(GUID_PROP_ATTRIBUTE, &property);
        }

        VARIANT value = {};
        VariantInit(&value);
        HRESULT value_hr = E_UNEXPECTED;
        if (SUCCEEDED(property_hr) && property) {
            value_hr = property->GetValue(edit_cookie, range, &value);
            property->Release();
        }
        if (range) {
            range->Release();
        }

        TfGuidAtom atom = 0;
        if (SUCCEEDED(value_hr) && (value.vt == VT_I4 || value.vt == VT_INT)) {
            atom = static_cast<TfGuidAtom>(value.lVal);
        }

        GUID attribute_guid = GUID_NULL;
        ITfCategoryMgr* category_manager = nullptr;
        HRESULT category_manager_hr = E_UNEXPECTED;
        if (without_com_) {
            category_manager_hr =
                cxxime::create_tsf_category_manager_without_com(&category_manager);
        } else {
            category_manager_hr = CoCreateInstance(
                CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                IID_ITfCategoryMgr, reinterpret_cast<void**>(&category_manager));
        }
        HRESULT atom_guid_hr = E_UNEXPECTED;
        if (SUCCEEDED(category_manager_hr) && category_manager && atom != 0) {
            atom_guid_hr = category_manager->GetGUID(atom, &attribute_guid);
        }
        if (category_manager) {
            category_manager->Release();
        }

        ITfDisplayAttributeMgr* display_manager = nullptr;
        HRESULT display_manager_hr = E_UNEXPECTED;
        if (without_com_) {
            display_manager_hr =
                cxxime::create_tsf_display_attribute_manager_without_com(&display_manager);
        } else {
            display_manager_hr = CoCreateInstance(
                CLSID_TF_DisplayAttributeMgr, nullptr, CLSCTX_INPROC_SERVER,
                IID_ITfDisplayAttributeMgr, reinterpret_cast<void**>(&display_manager));
        }
        ITfDisplayAttributeInfo* attribute_info = nullptr;
        CLSID owner = CLSID_NULL;
        HRESULT display_info_hr = E_UNEXPECTED;
        if (SUCCEEDED(display_manager_hr) && display_manager && SUCCEEDED(atom_guid_hr)) {
            display_info_hr = display_manager->GetDisplayAttributeInfo(
                attribute_guid, &attribute_info, &owner);
        }
        if (display_manager) {
            display_manager->Release();
        }

        TF_DISPLAYATTRIBUTE attribute = {};
        HRESULT attribute_hr = E_UNEXPECTED;
        if (SUCCEEDED(display_info_hr) && attribute_info) {
            attribute_hr = attribute_info->GetAttributeInfo(&attribute);
            attribute_info->Release();
        }

        const bool verified = SUCCEEDED(value_hr) && atom != 0 &&
                              SUCCEEDED(atom_guid_hr) &&
                              SUCCEEDED(display_info_hr) &&
                              SUCCEEDED(attribute_hr);
        cxxime::write_stage_trace("probe", "probe.display_attribute", {
            {"composition_id", composition_id_},
            {"element_id", element_id_},
            {"action", action_},
            {"manager_creation", without_com_ ? "without_com" : "com"},
            {"context_composition_hr", static_cast<int64_t>(context_composition_hr)},
            {"enum_hr", static_cast<int64_t>(enum_hr)},
            {"next_hr", static_cast<int64_t>(next_hr)},
            {"range_hr", static_cast<int64_t>(range_hr)},
            {"property_hr", static_cast<int64_t>(property_hr)},
            {"value_hr", static_cast<int64_t>(value_hr)},
            {"value_type", value.vt},
            {"atom", atom},
            {"category_manager_hr", static_cast<int64_t>(category_manager_hr)},
            {"atom_guid_hr", static_cast<int64_t>(atom_guid_hr)},
            {"attribute_guid", cxxime::stage_trace_guid(attribute_guid)},
            {"display_manager_hr", static_cast<int64_t>(display_manager_hr)},
            {"display_info_hr", static_cast<int64_t>(display_info_hr)},
            {"owner_clsid", cxxime::stage_trace_guid(owner)},
            {"attribute_hr", static_cast<int64_t>(attribute_hr)},
            {"attribute_type", attribute.bAttr},
            {"line_style", attribute.lsStyle},
            {"text_color_type", attribute.crText.type},
            {"background_color_type", attribute.crBk.type},
            {"line_color_type", attribute.crLine.type},
            {"result", verified ? "verified" : "incomplete"},
        });
        VariantClear(&value);
        return S_OK;
    }

private:
    ~DisplayAttributeEditSession() {
        context_->Release();
    }

    LONG ref_count_ = 1;
    ITfContext* context_ = nullptr;
    uint64_t composition_id_ = 0;
    DWORD element_id_ = TF_INVALID_UIELEMENTID;
    std::string action_;
    bool without_com_ = false;
};

} // namespace

void ProbeApp::trace_composition_display_attribute(
    ITfCandidateListUIElement* candidate,
    DWORD element_id,
    const char* action) {
    ITfDocumentMgr* document_manager = nullptr;
    const HRESULT document_hr = candidate
        ? candidate->GetDocumentMgr(&document_manager)
        : E_POINTER;
    ITfContext* context = nullptr;
    HRESULT context_hr = E_UNEXPECTED;
    if (SUCCEEDED(document_hr) && document_manager) {
        context_hr = document_manager->GetTop(&context);
        document_manager->Release();
    }

    HRESULT request_hr = E_UNEXPECTED;
    HRESULT edit_hr = E_UNEXPECTED;
    if (SUCCEEDED(context_hr) && context) {
        auto* edit_session = new (std::nothrow) DisplayAttributeEditSession(
            context, composition_id_, element_id, action,
            com_mode_ != ProbeComMode::sta);
        if (edit_session) {
            request_hr = context->RequestEditSession(
                client_id_, edit_session, TF_ES_READ | TF_ES_ASYNCDONTCARE, &edit_hr);
            edit_session->Release();
        } else {
            request_hr = E_OUTOFMEMORY;
        }
        context->Release();
    }

    cxxime::write_stage_trace("probe", "probe.display_attribute_request", {
        {"composition_id", composition_id_},
        {"element_id", element_id},
        {"action", action ? action : ""},
        {"document_hr", static_cast<int64_t>(document_hr)},
        {"context_hr", static_cast<int64_t>(context_hr)},
        {"request_hr", static_cast<int64_t>(request_hr)},
        {"edit_hr", static_cast<int64_t>(edit_hr)},
        {"result", SUCCEEDED(request_hr) ? "requested" : "failed"},
    });
}

} // namespace cxxime_probe
