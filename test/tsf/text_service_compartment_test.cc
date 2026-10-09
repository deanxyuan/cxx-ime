// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "pch.h"

#include "status_test_support.h"

namespace {

class ConversionCompartment : public tsf_test::HostCompartment {
public:
    TextService* service = nullptr;
    int writes = 0;
    STDMETHODIMP SetValue(TfClientId client, const VARIANT* data) override {
        ++writes;
        HostCompartment::SetValue(client, data);
        return service->OnChange(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    }
};

class ConversionManager : public tsf_test::HostThreadManager, public ITfCompartmentMgr {
public:
    ConversionCompartment compartment;
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (iid == IID_ITfCompartmentMgr) {
            *object = static_cast<ITfCompartmentMgr*>(this);
            AddRef();
            return S_OK;
        }
        return HostThreadManager::QueryInterface(iid, object);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return HostThreadManager::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return HostThreadManager::Release(); }
    STDMETHODIMP GetCompartment(REFGUID guid, ITfCompartment** out) override {
        *out = nullptr;
        if (guid != GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) {
            return E_NOTIMPL;
        }
        *out = &compartment;
        compartment.AddRef();
        return S_OK;
    }
    STDMETHODIMP ClearCompartment(TfClientId, REFGUID) override { return E_NOTIMPL; }
    STDMETHODIMP EnumCompartments(IEnumGUID**) override { return E_NOTIMPL; }
};

} // namespace

TEST(TextServiceCompartment, external_language_changes_and_caps_writeback_do_not_feed_back) {
    status_test::KeyboardState keyboard;
    keyboard.caps(false);
    ConversionManager manager;
    status_test::StatusFixture fixture(false);
    manager.document.top = &fixture.host;
    fixture.host.document = &manager.document;
    manager.compartment.service = &fixture.service;
    TextServiceTestPeer::set_thread_manager(fixture.service, &manager);
    TextServiceTestPeer::attach_conversion_compartment(fixture.service, &manager.compartment);
    ASSERT_TRUE(TextServiceTestPeer::connect(fixture.service, fixture.pipe));
    TextServiceTestPeer::start_status_dispatch(fixture.service);
    status_test::StatusProbe probe(fixture.service);
    fixture.focus();

    // A system language change can arrive before focus's deferred Caps confirmation.
    for (bool chinese : {false, true}) {
        const auto requests = fixture.language_requests.load();
        manager.compartment.value = chinese ? TF_CONVERSIONMODE_NATIVE : 0;
        ASSERT_EQ(fixture.service.OnChange(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION), S_OK);
        ASSERT_EQ(fixture.language_requests.load(), requests + 1);
        probe.expect_current(false, chinese);
        ASSERT_EQ(fixture.service.OnChange(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION), S_OK);
        ASSERT_EQ(fixture.language_requests.load(), requests + 1);
    }
    fixture.confirm();
    const auto requests = fixture.language_requests.load();
    const auto writes = manager.compartment.writes;
    for (bool caps : {true, false}) {
        keyboard.caps(caps);
        TextServiceTestPeer::poll_status(fixture.service);
        probe.expect_current(caps, !caps);
        ASSERT_TRUE(fixture.server_chinese.load());
        ASSERT_EQ(fixture.language_requests.load(), requests);
    }
    ASSERT_EQ(manager.compartment.writes, writes + 2);

    // A failed external change must not claim a successful switch on screen.
    fixture.reject_mode = true;
    manager.compartment.value = 0;
    ASSERT_EQ(fixture.service.OnChange(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION), S_OK);
    ASSERT_TRUE(TextServiceTestPeer::status(fixture.service).chinese_mode());
    fixture.reject_mode = false;
    ASSERT_EQ(fixture.service.OnChange(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION), S_OK);
    probe.expect_current(false, false);
}
