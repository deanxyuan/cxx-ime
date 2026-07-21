// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DIAGNOSTICS_HOST_TAKEOVER_UI_ELEMENT_SINK_H_
#define CXXIME_DIAGNOSTICS_HOST_TAKEOVER_UI_ELEMENT_SINK_H_

#include <msctf.h>

#include <atomic>

namespace cxxime_probe {

class ProbeApp;

class UiElementSink final : public ITfUIElementSink {
public:
    explicit UiElementSink(ProbeApp* app) : app_(app) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP BeginUIElement(DWORD element_id, BOOL* show) override;
    STDMETHODIMP UpdateUIElement(DWORD element_id) override;
    STDMETHODIMP EndUIElement(DWORD element_id) override;

private:
    ~UiElementSink() = default;

    std::atomic<ULONG> refs_{1};
    ProbeApp* app_ = nullptr;
};

} // namespace cxxime_probe

#endif // CXXIME_DIAGNOSTICS_HOST_TAKEOVER_UI_ELEMENT_SINK_H_
