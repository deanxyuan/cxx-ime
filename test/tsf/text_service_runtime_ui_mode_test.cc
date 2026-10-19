// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "activation_test_support.h"
#include "host_compatibility/host_classification_compatibility.h"
#include "status_test_support.h"

namespace {

class RuntimeManager : public tsf_test::ActivationManager, public ITfUIElementMgr {
public:
    tsf_test::HostContext* host = nullptr;
    TextService* service = nullptr;
    int candidate_begins = 0;
    int reading_begins = 0;
    int candidate_ends = 0;
    int writes_at_candidate_begin = -1;
    bool compatibility_at_begin = false;
    bool external_candidates = false;
    ITfUIElement* candidate_element = nullptr; // Borrowed while registered with the manager.
    std::wstring first_candidate;
    std::map<DWORD, bool> elements;
    DWORD next_element = 0;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (iid == IID_ITfUIElementMgr) {
            *object = static_cast<ITfUIElementMgr*>(this);
            AddRef();
            return S_OK;
        }
        return ActivationManager::QueryInterface(iid, object);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ActivationManager::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return ActivationManager::Release(); }
    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* sink, DWORD* cookie) override {
        if (iid == IID_ITfUIElementSink) {
            *cookie = TF_INVALID_COOKIE;
            return E_NOINTERFACE; // Diagnostic observers must not act as a second UI consumer.
        }
        return ActivationManager::AdviseSink(iid, sink, cookie);
    }
    STDMETHODIMP BeginUIElement(ITfUIElement* element, BOOL* show, DWORD* id) override {
        ITfCandidateListUIElement* candidate = nullptr;
        const bool is_candidate = SUCCEEDED(element->QueryInterface(
            IID_ITfCandidateListUIElement, reinterpret_cast<void**>(&candidate)));
        if (is_candidate) {
            candidate_element = element;
            ++candidate_begins;
            writes_at_candidate_begin = host->range.writes;
            compatibility_at_begin = TextServiceTestPeer::host_runtime_active(*service);
            if (compatibility_at_begin) {
                ASSERT_EQ(
                    std::string(cxxime_tsf::host_classification_compatibility_snapshot().action),
                    std::string("prepare"));
            }
            UINT count = 0;
            ASSERT_EQ(candidate->GetCount(&count), S_OK);
            ASSERT_EQ(count, 1u);
            BSTR text = nullptr;
            ASSERT_EQ(candidate->GetString(0, &text), S_OK);
            first_candidate.assign(text, SysStringLen(text));
            SysFreeString(text);
            candidate->Release();
        } else {
            ++reading_begins;
        }
        *id = ++next_element;
        elements[*id] = is_candidate;
        *show = is_candidate && external_candidates ? TRUE : FALSE;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD id) override {
        ASSERT_TRUE(elements.count(id) != 0);
        if (elements[id] && TextServiceTestPeer::host_runtime_active(*service)) {
            ASSERT_EQ(std::string(cxxime_tsf::host_classification_compatibility_snapshot().action),
                      std::string("prepare"));
        }
        return S_OK;
    }
    STDMETHODIMP EndUIElement(DWORD id) override {
        ASSERT_TRUE(elements.count(id) != 0);
        if (elements[id]) {
            ++candidate_ends;
            candidate_element = nullptr;
        }
        elements.erase(id);
        return S_OK;
    }
    STDMETHODIMP GetUIElement(DWORD, ITfUIElement**) override { return E_NOTIMPL; }
    STDMETHODIMP EnumUIElements(IEnumTfUIElements**) override { return E_NOTIMPL; }
};

struct RuntimeFixture {
    status_test::KeyboardState keyboard;
    RuntimeManager manager;
    status_test::StatusFixture fixture{false};
    status_test::StatusProbe probe{fixture.service};

    explicit RuntimeFixture(DWORD activation_flags = 0, bool expose_flags = true) {
        manager.host = &fixture.host;
        manager.service = &fixture.service;
        manager.expose_active_flags = expose_flags;
        manager.active_flags = TF_TMF_ACTIVATED | activation_flags;
        manager.document.top = &fixture.host;
        fixture.host.document = &manager.document;
        ASSERT_TRUE(TextServiceTestPeer::connect(fixture.service, fixture.pipe, false));
        TextServiceTestPeer::prepare_status_dispatch(fixture.service);
        // Establish initial visibility with one explicit focus transition. Activation may
        // publish the same snapshot twice; observing the first is not a transport barrier.
        manager.focused = FALSE;
        ASSERT_EQ(fixture.service.ActivateEx(&manager, 1, activation_flags), S_OK);
        manager.focused = TRUE;
        fixture.focus();
        TextServiceTestPeer::popup_only(fixture.service);
        if ((activation_flags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
            probe.expect_visibility_since(0, false);
        } else {
            probe.expect_current(false, true);
        }
    }
    ~RuntimeFixture() {
        ASSERT_EQ(fixture.service.Deactivate(), S_OK);
        ASSERT_TRUE(manager.elements.empty());
        ASSERT_TRUE(manager.sinks.empty());
        ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(fixture.service));
        ASSERT_EQ(manager.references, 1u);
    }
    bool apply(const cxxime::IPCResponse& response) {
        BOOL eaten = FALSE;
        const bool applied =
            TextServiceTestPeer::apply(fixture.service, &fixture.host, response, &eaten);
        ASSERT_TRUE(eaten != FALSE);
        return applied;
    }
};

cxxime::IPCResponse preedit_response(const char* text = "d", bool has_candidates = true) {
    cxxime::IPCResponse response = {};
    response.status = cxxime::IPCStatus::OK;
    response.composing = true;
    response.key_handled = true;
    response.ime_status.set_chinese_mode(true);
    strcpy_s(response.preedit, text);
    response.preedit_cursor = static_cast<uint32_t>(strlen(text));
    if (has_candidates) {
        response.candidate_count = 1;
        strcpy_s(response.candidates[0], "\xe5\x9c\xa8"); // 在
        response.page_current = 1;
        response.page_total = 1;
    }
    return response;
}

} // namespace

TEST(TextServiceRuntimeUiMode, first_composition_uses_current_mode_without_reactivation) {
    // Stable ordinary/UI-only modes and both transitions before the first input.
    for (int scenario = 0; scenario < 4; ++scenario) {
        const DWORD activation = scenario >= 2 ? TF_TMF_UIELEMENTENABLEDONLY : 0;
        RuntimeFixture test(activation);
        const bool host_mode = scenario == 1 || scenario == 2;
        test.manager.active_flags =
            TF_TMF_ACTIVATED | (host_mode ? TF_TMF_UIELEMENTENABLEDONLY : 0);
        const auto before = test.probe.mark();
        ASSERT_TRUE(test.apply(preedit_response()));
        ASSERT_EQ(TextServiceTestPeer::activation_flags(test.fixture.service), activation);
        ASSERT_EQ(test.manager.candidate_begins, 1);
        ASSERT_EQ(test.manager.reading_begins, host_mode ? 0 : 1);
        ASSERT_EQ(test.manager.compatibility_at_begin, host_mode);
        ASSERT_EQ(test.manager.first_candidate, std::wstring(L"\u5728"));
        if (host_mode) {
            ASSERT_EQ(test.manager.writes_at_candidate_begin, 0);
            ASSERT_EQ(test.fixture.host.range.text, std::wstring(L"d"));
            ASSERT_TRUE(!test.fixture.service.empty_composition_placeholder_active());
        } else {
            ASSERT_GT(test.manager.writes_at_candidate_begin, 0);
            ASSERT_TRUE(test.fixture.service.empty_composition_placeholder_active());
        }
        // The host owns candidates in every scenario; never show status on the way there.
        test.probe.expect_visibility_since(before, false);
        ASSERT_TRUE(!test.fixture.service.is_candidate_ui_element_shown());
        // Updates reuse the negotiated UI element and do not query modes on each character.
        const int queries = test.manager.active_flags_queries;
        ASSERT_TRUE(test.apply(preedit_response("dd")));
        ASSERT_EQ(test.manager.active_flags_queries, queries);
        ASSERT_EQ(test.manager.candidate_begins, 1);
        cxxime::IPCResponse commit = {};
        commit.status = cxxime::IPCStatus::OK;
        commit.ime_status.set_chinese_mode(true);
        strcpy_s(commit.commit_text, "\xe5\x9c\xa8");
        ASSERT_TRUE(test.apply(commit));
        ASSERT_EQ(test.fixture.host.range.text, std::wstring(L"\u5728"));
        ASSERT_EQ(test.manager.candidate_ends, 1);
        ASSERT_TRUE(TextServiceTestPeer::empty(test.fixture.service));
        if (host_mode) {
            test.probe.expect_visibility_since(before, false);
        }
    }
}

TEST(TextServiceRuntimeUiMode, focus_transition_hides_status_and_retains_mode_on_query_failure) {
    RuntimeFixture test;
    test.manager.active_flags = TF_TMF_ACTIVATED | TF_TMF_UIELEMENTENABLEDONLY;
    auto before = test.probe.mark();
    test.fixture.focus();
    test.probe.expect_visibility_since(before, false);
    ASSERT_TRUE(TextServiceTestPeer::host_runtime_active(test.fixture.service));
    const auto session = TextServiceTestPeer::session_id(test.fixture.service);
    // Missing interface and failed query both retain the last successful UI mode.
    for (int attempt = 0; attempt < 3; ++attempt) {
        before = test.probe.mark();
        test.manager.expose_active_flags = attempt != 1;
        test.manager.active_flags_result = attempt == 0 ? E_FAIL : S_OK;
        test.manager.active_flags =
            TF_TMF_ACTIVATED | (attempt == 2 ? TF_TMF_UIELEMENTENABLEDONLY : 0);
        test.fixture.focus();
        test.probe.expect_visibility_since(before, false);
        ASSERT_TRUE(TextServiceTestPeer::host_runtime_active(test.fixture.service));
        ASSERT_EQ(TextServiceTestPeer::session_id(test.fixture.service), session);
    }
    test.manager.active_flags = TF_TMF_ACTIVATED;
    before = test.probe.mark();
    test.fixture.focus();
    test.probe.expect_visibility_since(before, true);
    ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(test.fixture.service));
    ASSERT_EQ(TextServiceTestPeer::activation_flags(test.fixture.service), 0u);
}

TEST(TextServiceRuntimeUiMode, heartbeat_refreshes_mode_without_focus_or_input_and_is_throttled) {
    RuntimeFixture test;
    test.manager.active_flags = TF_TMF_ACTIVATED | TF_TMF_UIELEMENTENABLEDONLY;
    auto before = test.probe.mark();
    TextServiceTestPeer::poll_status(test.fixture.service, true);
    test.probe.expect_visibility_since(before, false);
    ASSERT_TRUE(TextServiceTestPeer::host_runtime_active(test.fixture.service));
    const int queries = test.manager.active_flags_queries;
    for (int tick = 0; tick < 3; ++tick) {
        TextServiceTestPeer::poll_status(test.fixture.service);
    }
    ASSERT_EQ(test.manager.active_flags_queries, queries);
    test.manager.active_flags = TF_TMF_ACTIVATED;
    test.fixture.server_chinese = false;
    before = test.probe.mark();
    TextServiceTestPeer::poll_status(test.fixture.service, true);
    test.probe.expect_current(false, false);
    test.probe.expect_since(before, true, false, false);
    ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(test.fixture.service));
}

TEST(TextServiceRuntimeUiMode, leaving_host_mode_with_failed_heartbeat_never_shows_cached_status) {
    RuntimeFixture test(TF_TMF_UIELEMENTENABLEDONLY);
    test.manager.active_flags = TF_TMF_ACTIVATED;
    test.fixture.reject_status = true;
    test.fixture.reject_sample = true;
    const auto before = test.probe.mark();
    TextServiceTestPeer::poll_status(test.fixture.service, true);
    ASSERT_EQ(TextServiceTestPeer::session_id(test.fixture.service), 0u);
    test.probe.expect_visibility_since(before, false);
}

TEST(TextServiceRuntimeUiMode, immersive_mode_tracks_current_flags_without_host_compatibility) {
    RuntimeFixture test;
    test.manager.active_flags = TF_TMF_ACTIVATED | TF_TMF_IMMERSIVEMODE;
    auto before = test.probe.mark();
    test.fixture.focus();
    ASSERT_TRUE(test.fixture.service.is_immersive_mode());
    ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(test.fixture.service));
    test.probe.expect_visibility_since(before, false);
    test.manager.active_flags = TF_TMF_ACTIVATED;
    before = test.probe.mark();
    test.fixture.focus();
    ASSERT_TRUE(!test.fixture.service.is_immersive_mode());
    test.probe.expect_visibility_since(before, true);
}

TEST(TextServiceRuntimeUiMode, unsupported_current_mode_uses_activation_flags) {
    RuntimeFixture test(TF_TMF_UIELEMENTENABLEDONLY, false);
    const auto before = test.probe.mark();
    test.fixture.focus();
    ASSERT_TRUE(test.apply(preedit_response()));
    ASSERT_EQ(test.manager.active_flags_queries, 0);
    ASSERT_EQ(test.manager.reading_begins, 0);
    ASSERT_TRUE(test.manager.compatibility_at_begin);
    test.probe.expect_visibility_since(before, false);
    cxxime::IPCResponse cancel = {};
    cancel.status = cxxime::IPCStatus::OK;
    cancel.key_handled = true;
    cancel.ime_status.set_chinese_mode(true);
    ASSERT_TRUE(test.apply(cancel));
    ASSERT_TRUE(TextServiceTestPeer::empty(test.fixture.service));
}

TEST(TextServiceRuntimeUiMode, mode_change_preserves_active_candidates_and_commit_continue_order) {
    for (bool heartbeat : {false, true}) {
        RuntimeFixture test;
        const auto initial = test.probe.mark();
        ASSERT_TRUE(test.apply(preedit_response()));
        test.probe.expect_visibility_since(initial, false);
        const int writes = test.fixture.host.range.writes;
        const int starts = test.fixture.host.starts;
        const int ends = test.fixture.host.composition.ends;
        test.manager.active_flags = TF_TMF_ACTIVATED | TF_TMF_UIELEMENTENABLEDONLY;
        if (heartbeat) {
            TextServiceTestPeer::poll_status(test.fixture.service, true);
            ASSERT_EQ(test.fixture.host.range.writes, writes);
            ASSERT_EQ(test.fixture.host.starts, starts);
            ASSERT_EQ(test.fixture.host.composition.ends, ends);
        }
        const auto before = test.probe.mark();
        auto response = preedit_response();
        strcpy_s(response.commit_text, "\xe5\x9c\xa8");
        ASSERT_TRUE(test.apply(response));
        ASSERT_EQ(test.manager.candidate_begins, 1);
        ASSERT_EQ(test.manager.candidate_ends, 0);
        ASSERT_EQ(test.manager.reading_begins, 1);
        ASSERT_EQ(test.manager.elements.size(), size_t{1});
        ASSERT_EQ(test.fixture.host.composition.ends, ends + 1);
        ASSERT_EQ(test.fixture.host.starts, starts + 1);
        const auto& texts = test.fixture.host.range.written_texts;
        ASSERT_EQ(texts[texts.size() - 2], std::wstring(L"\u5728"));
        ASSERT_EQ(texts.back(), std::wstring(L"d"));
        ASSERT_TRUE(!test.fixture.service.empty_composition_placeholder_active());
        test.probe.expect_visibility_since(before, false);
    }
}

TEST(TextServiceRuntimeUiMode, reactivation_discards_the_previous_runtime_mode) {
    status_test::KeyboardState keyboard;
    RuntimeManager manager;
    status_test::StatusFixture fixture(false);
    manager.host = &fixture.host;
    manager.service = &fixture.service;
    manager.document.top = &fixture.host;
    fixture.host.document = &manager.document;
    ASSERT_TRUE(TextServiceTestPeer::connect(fixture.service, fixture.pipe, false));
    for (int activation = 0; activation < 2; ++activation) {
        TextServiceTestPeer::prepare_status_dispatch(fixture.service);
        status_test::StatusProbe probe(fixture.service);
        manager.expose_active_flags = activation == 0;
        manager.active_flags = TF_TMF_ACTIVATED;
        manager.focused = FALSE;
        ASSERT_EQ(fixture.service.ActivateEx(&manager, 1, 0), S_OK);
        manager.focused = TRUE;
        fixture.focus();
        ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(fixture.service));
        probe.expect_current(false, true);
        if (activation == 0) {
            manager.active_flags |= TF_TMF_UIELEMENTENABLEDONLY;
            const auto before = probe.mark();
            fixture.focus();
            probe.expect_visibility_since(before, false);
            ASSERT_TRUE(TextServiceTestPeer::host_runtime_active(fixture.service));
        }
        ASSERT_EQ(fixture.service.Deactivate(), S_OK);
        ASSERT_TRUE(!TextServiceTestPeer::host_runtime_active(fixture.service));
        ASSERT_TRUE(manager.sinks.empty());
        ASSERT_EQ(manager.references, 1u);
    }
}

TEST(TextServiceRuntimeUiMode, empty_candidate_list_keeps_host_preedit_until_commit) {
    for (DWORD flags : {DWORD{0}, DWORD{TF_TMF_UIELEMENTENABLEDONLY}}) {
        RuntimeFixture test(flags);
        auto before = test.probe.mark();
        ASSERT_TRUE(test.apply(preedit_response()));
        test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kHost);
        const int starts = test.fixture.host.starts;
        const int ends = test.fixture.host.composition.ends;
        for (const char* text : {"dx", "dxx"}) {
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response(text, false)));
            ASSERT_EQ(test.manager.candidate_begins, 1);
            ASSERT_EQ(test.manager.candidate_ends, 1);
            ASSERT_TRUE(test.fixture.service.is_composing());
            ASSERT_EQ(test.fixture.host.starts, starts);
            ASSERT_EQ(test.fixture.host.composition.ends, ends);
            ASSERT_EQ(TextServiceTestPeer::preedit(test.fixture.service), std::string(text));
            ASSERT_TRUE(TextServiceTestPeer::candidate_page(test.fixture.service).items.empty());
            if (flags != 0) {
                ASSERT_EQ(test.fixture.host.range.text, std::wstring(text, text + strlen(text)));
            }
            test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kHost);
            test.probe.expect_visibility_since(before, false);
        }
        before = test.probe.mark();
        ASSERT_TRUE(test.apply(preedit_response())); // Backspace restores a valid prefix.
        ASSERT_EQ(test.manager.candidate_begins, 2);
        ASSERT_EQ(test.manager.candidate_ends, 1);
        test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kHost);

        cxxime::IPCResponse commit = {};
        commit.status = cxxime::IPCStatus::OK;
        commit.ime_status.set_chinese_mode(true);
        strcpy_s(commit.commit_text, "\xe5\x9c\xa8");
        before = test.probe.mark();
        ASSERT_TRUE(test.apply(commit));
        ASSERT_EQ(test.fixture.host.range.text, std::wstring(L"\u5728"));
        ASSERT_TRUE(TextServiceTestPeer::empty(test.fixture.service));
        ASSERT_EQ(test.manager.candidate_ends, 2);
        test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kNone);

        before = test.probe.mark();
        ASSERT_TRUE(test.apply(preedit_response("x", false)));
        ASSERT_EQ(test.manager.candidate_begins, 2);
        test.probe.expect_candidate_ownership_since(before, flags ? cxxime::UiOwnership::kHost
                                                                  : cxxime::UiOwnership::kExternal);
    }
}

TEST(TextServiceRuntimeUiMode, empty_candidate_list_preserves_explicit_external_permission) {
    for (DWORD flags : {DWORD{0}, DWORD{TF_TMF_UIELEMENTENABLEDONLY}}) {
        for (bool use_show : {false, true}) {
            RuntimeFixture test(flags);
            test.manager.external_candidates = !use_show;
            auto before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response()));
            test.probe.expect_candidate_ownership_since(
                before, use_show ? cxxime::UiOwnership::kHost : cxxime::UiOwnership::kExternal);
            if (use_show) {
                before = test.probe.mark();
                ASSERT_TRUE(test.manager.candidate_element != nullptr);
                ASSERT_EQ(test.manager.candidate_element->Show(TRUE), S_OK);
                test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kExternal);
            }
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response("dx", false)));
            ASSERT_EQ(test.manager.candidate_ends, 1);
            ASSERT_TRUE(test.fixture.service.is_candidate_ui_element_shown());
            ASSERT_EQ(TextServiceTestPeer::preedit(test.fixture.service), std::string("dx"));
            test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kExternal);

            // A new list must honor a new host decision instead of retaining the old permission.
            test.manager.external_candidates = false;
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response()));
            ASSERT_EQ(test.manager.candidate_begins, 2);
            test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kHost);
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response("dx", false)));
            test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kHost);
        }
    }
}

TEST(TextServiceRuntimeUiMode, initial_empty_list_uses_mode_defaults_after_cancel_or_focus_loss) {
    for (DWORD flags : {DWORD{0}, DWORD{TF_TMF_UIELEMENTENABLEDONLY}}) {
        for (bool lose_focus : {false, true}) {
            RuntimeFixture test(flags);
            const auto default_owner =
                flags ? cxxime::UiOwnership::kHost : cxxime::UiOwnership::kExternal;
            auto before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response("x", false)));
            ASSERT_EQ(test.manager.candidate_begins, 0);
            ASSERT_TRUE(test.fixture.service.is_composing());
            test.probe.expect_candidate_ownership_since(before, default_owner);
            if (flags != 0) {
                ASSERT_EQ(test.fixture.host.range.text, std::wstring(L"x"));
            }

            // Negotiate the opposite of the default, then end this input target/composition.
            test.manager.external_candidates = flags != 0;
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response()));
            test.probe.expect_candidate_ownership_since(
                before, flags ? cxxime::UiOwnership::kExternal : cxxime::UiOwnership::kHost);
            before = test.probe.mark();
            if (lose_focus) {
                ASSERT_EQ(test.fixture.service.OnKillThreadFocus(), S_OK);
            } else {
                cxxime::IPCResponse cancel = {};
                cancel.status = cxxime::IPCStatus::OK;
                cancel.key_handled = true;
                cancel.ime_status.set_chinese_mode(true);
                ASSERT_TRUE(test.apply(cancel));
            }
            ASSERT_TRUE(TextServiceTestPeer::empty(test.fixture.service));
            test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kNone);
            if (lose_focus) {
                before = test.probe.mark();
                test.fixture.focus();
                test.probe.expect_candidate_ownership_since(before, cxxime::UiOwnership::kNone);
            }
            before = test.probe.mark();
            ASSERT_TRUE(test.apply(preedit_response("x", false)));
            ASSERT_EQ(test.manager.candidate_begins, 1);
            test.probe.expect_candidate_ownership_since(before, default_owner);
        }
    }
}
