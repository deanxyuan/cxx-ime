// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_CANDIDATE_UI_ELEMENT_H_
#define CXXIME_TSF_CANDIDATE_UI_ELEMENT_H_

#include "pch.h"
#include <cxxime/candidate.h>
#include <string>
#include <vector>

class TextService;

class CandidateUIElement : public ITfCandidateListUIElementBehavior {
public:
    explicit CandidateUIElement(TextService* service);

static constexpr DWORD kPublishedUpdatedFlags =
    TF_CLUIE_COUNT | TF_CLUIE_SELECTION | TF_CLUIE_STRING |
    TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfUIElement
    STDMETHODIMP GetDescription(BSTR* pbstrDescription) override;
    STDMETHODIMP GetGUID(GUID* pguid) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* show) override;

    // ITfCandidateListUIElement
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** doc_mgr) override;
    STDMETHODIMP GetCount(UINT* count) override;
    STDMETHODIMP GetSelection(UINT* index) override;
    STDMETHODIMP GetString(UINT index, BSTR* text) override;
    STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* page_count) override;
    STDMETHODIMP SetPageIndex(UINT* index, UINT page_count) override;
    STDMETHODIMP GetCurrentPage(UINT* page) override;

    // ITfCandidateListUIElementBehavior
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;

    void set_page(const cxxime::CandidatePage& page, int page_current, int page_total);
    bool begin(ITfThreadMgr* thread_mgr);
    void notify_update(ITfThreadMgr* thread_mgr);
    void end(ITfThreadMgr* thread_mgr);
    bool wants_external_window() const { return !_active || _show_external != FALSE; }
    bool is_active() const { return _active; }

private:
    ~CandidateUIElement();

    HRESULT query_ui_element_mgr(ITfThreadMgr* thread_mgr, ITfUIElementMgr** ui_mgr) const;
    void capture_document_mgr(ITfThreadMgr* thread_mgr);
    void release_document_mgr();

    LONG _cRef = 1;
    TextService* _service = nullptr;
    bool _active = false;
    BOOL _show_external = TRUE;
    BOOL _shown = FALSE;
    DWORD _ui_element_id = TF_INVALID_UIELEMENTID;
    ITfDocumentMgr* _document_mgr = nullptr;
    std::vector<std::wstring> _candidates;
    UINT _selection = 0;
};

#endif // CXXIME_TSF_CANDIDATE_UI_ELEMENT_H_
