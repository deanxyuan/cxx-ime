// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "candidate_ui_element.h"
#include "globals.h"
#include "text_service.h"
#include <algorithm>

namespace {

std::wstring utf8_to_wstring(const std::string& text) {
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], len);
    return result;
}

} // namespace

CandidateUIElement::CandidateUIElement(TextService* service)
    : _service(service) {}

CandidateUIElement::~CandidateUIElement() {
    release_document_mgr();
}

STDMETHODIMP CandidateUIElement::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, __uuidof(ITfIntegratableCandidateListUIElement))) {
        *ppvObj = static_cast<ITfIntegratableCandidateListUIElement*>(this);
    } else if (IsEqualIID(riid, IID_ITfUIElement) ||
               IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
               IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
        *ppvObj = static_cast<ITfCandidateListUIElementBehavior*>(this);
    }

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CandidateUIElement::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) CandidateUIElement::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

STDMETHODIMP CandidateUIElement::GetDescription(BSTR* pbstrDescription) {
    if (!pbstrDescription)
        return E_INVALIDARG;
    *pbstrDescription = SysAllocString(L"CxxIME Candidate List");
    return *pbstrDescription ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUIElement::GetGUID(GUID* pguid) {
    if (!pguid)
        return E_INVALIDARG;
    *pguid = c_guidCandidateUIElement;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::Show(BOOL show) {
    _shown = show;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::IsShown(BOOL* show) {
    if (!show)
        return E_INVALIDARG;
    *show = _shown;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetUpdatedFlags(DWORD* flags) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetUpdatedFlags");
    if (!flags)
        return E_INVALIDARG;
    *flags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
             TF_CLUIE_STRING | TF_CLUIE_CURRENTPAGE;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetDocumentMgr(ITfDocumentMgr** doc_mgr) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetDocumentMgr");
    if (!doc_mgr)
        return E_INVALIDARG;
    *doc_mgr = nullptr;
    if (!_document_mgr)
        return E_FAIL;
    _document_mgr->AddRef();
    *doc_mgr = _document_mgr;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetCount(UINT* count) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetCount", true);
    if (!count)
        return E_INVALIDARG;
    *count = static_cast<UINT>(_candidates.size());
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetSelection(UINT* index) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetSelection");
    if (!index)
        return E_INVALIDARG;
    *index = _selection;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetString(UINT index, BSTR* text) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetString", true);
    if (!text)
        return E_INVALIDARG;
    *text = nullptr;
    if (index >= _candidates.size())
        return E_INVALIDARG;
    const auto& candidate = _candidates[index];
    *text = SysAllocStringLen(candidate.c_str(), static_cast<UINT>(candidate.size()));
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateUIElement::GetPageIndex(UINT* index, UINT size, UINT* page_count) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetPageIndex");
    if (!page_count)
        return E_INVALIDARG;
    *page_count = 1;
    if (!index)
        return S_OK;
    if (size < 1)
        return E_INVALIDARG;
    index[0] = 0;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::SetPageIndex(UINT* index, UINT page_count) {
    if (page_count > 0 && !index)
        return E_INVALIDARG;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetCurrentPage(UINT* page) {
    if (_service)
        _service->trace_ui_element_method("candidate", "GetCurrentPage");
    if (!page)
        return E_INVALIDARG;
    *page = 0;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::SetSelection(UINT index) {
    if (index >= _candidates.size())
        return E_INVALIDARG;
    _selection = index;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::Finalize() {
    if (!_service || _candidates.empty())
        return E_FAIL;
    return _service->select_candidate_from_ui(_selection) ? S_OK : E_FAIL;
}

STDMETHODIMP CandidateUIElement::Abort() {
    if (_service)
        _service->abort_candidate_ui_from_tsf();
    return S_OK;
}

STDMETHODIMP CandidateUIElement::SetIntegrationStyle(GUID integration_style) {
    UNREFERENCED_PARAMETER(integration_style);
    return S_OK;
}

STDMETHODIMP CandidateUIElement::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* selection_style) {
    if (!selection_style)
        return E_INVALIDARG;
    *selection_style = _selection_style;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::OnKeyDown(WPARAM w_param, LPARAM l_param, BOOL* eaten) {
    UNREFERENCED_PARAMETER(w_param);
    UNREFERENCED_PARAMETER(l_param);
    if (!eaten)
        return E_INVALIDARG;
    *eaten = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::ShowCandidateNumbers(BOOL* show) {
    if (!show)
        return E_INVALIDARG;
    *show = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUIElement::FinalizeExactCompositionString() {
    return _service ? _service->finalize_exact_candidate_ui_from_tsf() : E_FAIL;
}

void CandidateUIElement::set_page(const cxxime::CandidatePage& page,
                                  int page_current,
                                  int page_total) {
    UNREFERENCED_PARAMETER(page_current);
    UNREFERENCED_PARAMETER(page_total);
    _candidates.clear();
    _candidates.reserve(page.candidates.size());
    for (const auto& candidate : page.candidates)
        _candidates.push_back(utf8_to_wstring(candidate.text));

    if (_candidates.empty()) {
        _selection = 0;
    } else if (page.highlighted >= 0 &&
               static_cast<size_t>(page.highlighted) < _candidates.size()) {
        _selection = static_cast<UINT>(page.highlighted);
    } else {
        _selection = std::min<UINT>(_selection, static_cast<UINT>(_candidates.size() - 1));
    }
}

bool CandidateUIElement::begin(ITfThreadMgr* thread_mgr) {
    if (_active)
        return wants_external_window();

    ITfUIElementMgr* ui_mgr = nullptr;
    if (FAILED(query_ui_element_mgr(thread_mgr, &ui_mgr)) || !ui_mgr) {
        _show_external = TRUE;
        return true;
    }

    capture_document_mgr(thread_mgr);
    _show_external = TRUE;
    DWORD element_id = TF_INVALID_UIELEMENTID;
    HRESULT hr = ui_mgr->BeginUIElement(
        static_cast<ITfCandidateListUIElementBehavior*>(this), &_show_external, &element_id);
    ui_mgr->Release();

    if (FAILED(hr)) {
        release_document_mgr();
        _show_external = TRUE;
        return true;
    }

    _active = true;
    _ui_element_id = element_id;
    _shown = _show_external;
    return wants_external_window();
}

void CandidateUIElement::notify_update(ITfThreadMgr* thread_mgr) {
    if (!_active)
        return;
    capture_document_mgr(thread_mgr);
    ITfUIElementMgr* ui_mgr = nullptr;
    if (SUCCEEDED(query_ui_element_mgr(thread_mgr, &ui_mgr)) && ui_mgr) {
        ui_mgr->UpdateUIElement(_ui_element_id);
        ui_mgr->Release();
    }
}

void CandidateUIElement::end(ITfThreadMgr* thread_mgr) {
    if (!_active)
        return;

    ITfUIElementMgr* ui_mgr = nullptr;
    if (SUCCEEDED(query_ui_element_mgr(thread_mgr, &ui_mgr)) && ui_mgr) {
        ui_mgr->EndUIElement(_ui_element_id);
        ui_mgr->Release();
    }

    _active = false;
    _show_external = TRUE;
    _shown = FALSE;
    _ui_element_id = TF_INVALID_UIELEMENTID;
    release_document_mgr();
}

HRESULT CandidateUIElement::query_ui_element_mgr(ITfThreadMgr* thread_mgr,
                                                  ITfUIElementMgr** ui_mgr) const {
    if (!ui_mgr)
        return E_INVALIDARG;
    *ui_mgr = nullptr;
    if (!thread_mgr)
        return E_FAIL;
    return thread_mgr->QueryInterface(IID_ITfUIElementMgr, reinterpret_cast<void**>(ui_mgr));
}

void CandidateUIElement::capture_document_mgr(ITfThreadMgr* thread_mgr) {
    if (!thread_mgr)
        return;

    ITfDocumentMgr* doc_mgr = nullptr;
    if (FAILED(thread_mgr->GetFocus(&doc_mgr)) || !doc_mgr)
        return;

    if (_document_mgr != doc_mgr) {
        release_document_mgr();
        _document_mgr = doc_mgr;
    } else {
        doc_mgr->Release();
    }
}

void CandidateUIElement::release_document_mgr() {
    if (_document_mgr) {
        _document_mgr->Release();
        _document_mgr = nullptr;
    }
}
