// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"
#include "globals.h"
#include "edit_session.h"
#include "display_attribute.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include "preedit_mode.h"
#include <cstring>
#include <shellapi.h>

TextService::TextService() {}

TextService::~TextService() {}

// IUnknown
STDMETHODIMP TextService::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj)
        return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextInputProcessor))
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
        *ppvObj = static_cast<ITfCompositionSink*>(this);
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
        *ppvObj = static_cast<ITfThreadFocusSink*>(this);
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
        *ppvObj = static_cast<ITfDisplayAttributeProvider*>(this);

    if (*ppvObj) {
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) TextService::AddRef() {
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) TextService::Release() {
    LONG cr = InterlockedDecrement(&_cRef);
    if (cr == 0)
        delete this;
    return cr;
}

// ITfTextInputProcessorEx
STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    CXXIME_LOG(L"ActivateEx: clientId=%u, flags=%u", tid, dwFlags);

    _load_config();

    _threadMgr = ptim;
    _threadMgr->AddRef();
    _clientId = tid;
    _activateFlags = dwFlags;

    _register_key_event_sink();
    _register_preserved_key();

    // Create candidate window (use HWND_MESSAGE parent since TSF runs in-app)
    _candidateWindow.create(nullptr, _config);
    _candidateWindow.set_layout(_config.layout);
    _candidateWindow.set_click_callback([this](int index) {
        cxxime::IPCResponse resp = {};
        if (_client.select_candidate(_sessionId, index, resp)) {
            if (resp.commit_text[0] != '\0') {
                std::wstring commit_text;
                int len = MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, nullptr, 0);
                if (len > 0) {
                    commit_text.resize(len - 1);
                    MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, &commit_text[0], len);
                }
                if (!commit_text.empty()) {
                    insert_text(commit_text);
                    // Need ITfContext to end composition — get from thread manager
                    ITfDocumentMgr* pDocMgr = nullptr;
                    if (SUCCEEDED(_threadMgr->GetFocus(&pDocMgr)) && pDocMgr) {
                        ITfContext* pContext = nullptr;
                        if (SUCCEEDED(pDocMgr->GetBase(&pContext)) && pContext) {
                            _end_composition(pContext);
                            pContext->Release();
                        }
                        pDocMgr->Release();
                    }
                    _composing = false;
                }
            }
            _candidateWindow.set_preedit("");
            _candidateWindow.hide();
        }
    });

    // Connect to server
    if (_client.connect()) {
        _client.start_session(_sessionId);
        CXXIME_LOG(L"Connected to server, sessionId=%u", _sessionId);
    } else {
        CXXIME_LOG(L"Failed to connect to server");
    }

    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    CXXIME_LOG(L"Deactivate: sessionId=%u", _sessionId);
    if (_sessionId) {
        // Commit any pending composition before ending session
        if (_composing) {
            cxxime::IPCResponse resp = {};
            _client.commit_composition(_sessionId, resp);
            if (resp.commit_text[0] != '\0' && _threadMgr) {
                std::wstring commit_text;
                int len = MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, nullptr, 0);
                if (len > 0) {
                    commit_text.resize(len - 1);
                    MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, &commit_text[0], len);
                }
                if (!commit_text.empty())
                    insert_text(commit_text, true);  // sync for Deactivate
            }
            _composing = false;
        }
        _client.end_session(_sessionId);
        _sessionId = 0;
    }
    _client.disconnect();

    _candidateWindow.destroy();
    _unregister_key_event_sink();
    _unregister_preserved_key();

    if (_threadMgr) {
        _threadMgr->Release();
        _threadMgr = nullptr;
    }
    _clientId = TF_CLIENTID_NULL;

    return S_OK;
}

// ITfKeyEventSink
STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = _should_eat_key(wParam);
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;

    uint32_t modifiers = _get_modifiers();
    CXXIME_LOG(L"OnKeyDown: vk=%u, mods=%u, composing=%d", (unsigned int)wParam, modifiers, _composing);

    cxxime::IPCResponse response = {};
    bool ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);

    // If IPC failed, try to reconnect and re-create session
    if (!ok) {
        if (_client.connect()) {
            _client.start_session(_sessionId);
            CXXIME_LOG(L"Reconnected, new sessionId=%u", _sessionId);
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
        }
    }

    if (ok) {
        _chinese_mode = !response.ascii_mode;

        if (response.commit_text[0] != '\0') {
            // Commit text
            _candidateWindow.hide();
            _candidateWindow.set_preedit("");
            std::wstring commit_text;
            int len = MultiByteToWideChar(CP_UTF8, 0, response.commit_text, -1, nullptr, 0);
            if (len > 0) {
                commit_text.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, response.commit_text, -1, &commit_text[0], len);
            }
            if (!commit_text.empty()) {
                insert_text(commit_text);
                _end_composition(pic);
                _composing = false;
                *pfEaten = TRUE;
            }
        } else if (response.preedit[0] != '\0') {
            // Decode preedit
            std::wstring preedit;
            int len = MultiByteToWideChar(CP_UTF8, 0, response.preedit, -1, nullptr, 0);
            if (len > 0) {
                preedit.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, response.preedit, -1, &preedit[0], len);
            }

            // Decode candidates
            std::vector<std::wstring> candidate_texts;
            for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                int clen = MultiByteToWideChar(CP_UTF8, 0, response.candidates[i], -1, nullptr, 0);
                if (clen > 0) {
                    std::wstring ct(clen - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, response.candidates[i], -1, &ct[0], clen);
                    candidate_texts.push_back(std::move(ct));
                }
            }

            auto decision = cxxime_tsf::decide_preedit(
                _config.inline_preedit, _config.preedit_type, preedit, candidate_texts);

            if (decision.start_composition) {
                if (!_composing) _start_composition(pic);
                update_composition(pic, decision.inline_text);
            } else {
                if (_composing && _composition) _end_composition(pic);
                _composing = true;
            }
            *pfEaten = TRUE;

            _candidateWindow.set_preedit(decision.show_preedit_in_popup ? response.preedit : "");

            if (response.candidate_count > 0) {
                cxxime::CandidatePage page;
                page.highlighted = (int)response.highlighted;
                for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                    cxxime::Candidate c;
                    c.text = response.candidates[i];
                    page.candidates.push_back(std::move(c));
                }
                _candidateWindow.update(page);

                POINT pt = {};
                GUITHREADINFO gti = { sizeof(gti) };
                if (GetGUIThreadInfo(GetCurrentThreadId(), &gti)) {
                    pt.x = gti.rcCaret.left;
                    pt.y = gti.rcCaret.top;
                    ClientToScreen(gti.hwndCaret, &pt);
                } else {
                    GetCaretPos(&pt);
                    ClientToScreen(GetFocus(), &pt);
                }
                _candidateWindow.set_position(pt.x, pt.y + 20);
                _candidateWindow.show();
            } else {
                _candidateWindow.hide();
            }
        } else if (response.status == cxxime::IPCStatus::OK) {
            // Server accepted but no commit and no preedit
            // (e.g. Escape cleared the buffer)
            _candidateWindow.hide();
            _candidateWindow.set_preedit("");
            _end_composition(pic);
            _composing = false;
            *pfEaten = TRUE;
        }
    }

    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    _send_modifier_key_up(wParam);
    return S_OK;
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (IsEqualGUID(rguid, c_guidPreservedKey_Toggle) && !_composing) {
        _chinese_mode = !_chinese_mode;
        CXXIME_LOG(L"Mode toggled (preserved key): %s", _chinese_mode ? L"Chinese" : L"English");
        *pfEaten = TRUE;
    } else {
        *pfEaten = FALSE;
    }
    return S_OK;
}

// ITfCompositionSink
STDMETHODIMP TextService::OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) {
    _composing = false;
    if (_composition) {
        _composition->Release();
        _composition = nullptr;
    }
    return S_OK;
}

// ITfThreadFocusSink
STDMETHODIMP TextService::OnSetThreadFocus() {
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    return S_OK;
}

// ITfDisplayAttributeProvider
STDMETHODIMP TextService::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum)
        return E_INVALIDARG;
    auto* pEnum = new (std::nothrow) ::EnumDisplayAttributeInfo();
    *ppEnum = pEnum;
    return pEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP TextService::GetDisplayAttributeInfo(REFGUID rguid, ITfDisplayAttributeInfo** ppInfo) {
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

// Helpers
HRESULT TextService::insert_text(const std::wstring& text, bool sync) {
    if (!_threadMgr || text.empty())
        return E_FAIL;

    ITfDocumentMgr* pDocMgr = nullptr;
    if (FAILED(_threadMgr->GetFocus(&pDocMgr)) || !pDocMgr)
        return E_FAIL;

    ITfContext* pContext = nullptr;
    if (FAILED(pDocMgr->GetBase(&pContext)) || !pContext) {
        pDocMgr->Release();
        return E_FAIL;
    }

    EditSession* pEditSession = new (std::nothrow) EditSession(this, pContext);
    if (!pEditSession) {
        pContext->Release();
        pDocMgr->Release();
        return E_OUTOFMEMORY;
    }

    pEditSession->set_action(EditSession::Action::INSERT_TEXT, text);

    HRESULT hr = E_FAIL;
    DWORD flags = sync ? (TF_ES_READWRITE | TF_ES_ASYNCDONTCARE) : (TF_ES_READWRITE | TF_ES_ASYNC);
    pContext->RequestEditSession(_clientId, pEditSession, flags, &hr);

    pEditSession->Release();
    pContext->Release();
    pDocMgr->Release();
    return hr;
}

void TextService::update_composition(ITfContext* pic, const std::wstring& preedit) {
    if (!_composing || !pic)
        return;

    EditSession* pSession = new (std::nothrow) EditSession(this, pic);
    if (!pSession)
        return;

    pSession->set_action(EditSession::Action::UPDATE_COMPOSITION, preedit);

    HRESULT hr = E_FAIL;
    pic->RequestEditSession(_clientId, pSession, TF_ES_READWRITE | TF_ES_ASYNC, &hr);

    pSession->Release();
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

HRESULT TextService::_start_composition(ITfContext* pic) {
    if (_composing)
        return S_OK;

    EditSession* pSession = new (std::nothrow) EditSession(this, pic);
    if (!pSession)
        return E_OUTOFMEMORY;

    pSession->set_action(EditSession::Action::START_COMPOSITION);

    HRESULT hr = E_FAIL;
    pic->RequestEditSession(_clientId, pSession, TF_ES_READWRITE | TF_ES_ASYNC, &hr);

    pSession->Release();
    return hr;
}

HRESULT TextService::_end_composition(ITfContext* pic) {
    if (!_composing || !_composition)
        return S_OK;

    EditSession* pSession = new (std::nothrow) EditSession(this, pic);
    if (!pSession)
        return E_OUTOFMEMORY;

    pSession->set_action(EditSession::Action::END_COMPOSITION);

    HRESULT hr = E_FAIL;
    pic->RequestEditSession(_clientId, pSession, TF_ES_READWRITE | TF_ES_ASYNC, &hr);

    pSession->Release();
    return hr;
}

bool TextService::_should_eat_key(WPARAM vk) const {
    // Ctrl/Alt combinations should pass through (Ctrl+C, Ctrl+V, Alt+Tab, etc.)
    BYTE kb[256] = {};
    if (GetKeyboardState(kb)) {
        if ((kb[VK_CONTROL] & 0x80) || (kb[VK_MENU] & 0x80))
            return false;
    }

    // In English mode, only eat keys when already composing
    // (user started composing then switched — let them finish)
    if (!_chinese_mode) {
        if (_composing) {
            if (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_SPACE || vk == VK_RETURN)
                return true;
            if (vk >= '1' && vk <= '9')
                return true;
        }
        return false;
    }

    // Chinese mode: when composing, eat keys that the IME handles
    if (_composing) {
        if (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_SPACE || vk == VK_RETURN)
            return true;
        if (vk >= '1' && vk <= '9')
            return true;
        if (vk == VK_PRIOR || vk == VK_NEXT)
            return true;
        if (vk == VK_UP || vk == VK_DOWN)
            return true;
    }

    // Chinese mode: eat letter keys to start/continue composition
    if (vk >= 'A' && vk <= 'Z')
        return true;

    return false;
}

uint32_t TextService::_get_modifiers() const {
    BYTE kb[256] = {};
    uint32_t mods = 0;
    if (GetKeyboardState(kb)) {
        if (kb[VK_SHIFT] & 0x80)
            mods |= 0x01;
        if (kb[VK_CONTROL] & 0x80)
            mods |= 0x02;
        if (kb[VK_MENU] & 0x80)
            mods |= 0x04;
    }
    return mods;
}

void TextService::_load_config() {
    _config.load(cxxime::data_path("default.json"));
}

void TextService::_send_modifier_key_up(WPARAM wParam) {
    if (!_sessionId)
        return;

    bool is_modifier = (wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_SHIFT ||
                        wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == VK_CONTROL ||
                        wParam == VK_CAPITAL);
    if (!is_modifier)
        return;

    cxxime::IPCResponse resp = {};
    _client.process_key(_sessionId, (uint32_t)wParam, 0, resp, true);

    // Sync ascii_mode state from engine
    _chinese_mode = !resp.ascii_mode;
}
