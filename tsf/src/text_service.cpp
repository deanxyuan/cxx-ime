// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"
#include "globals.h"
#include "edit_session.h"
#include "display_attribute.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include "preedit_mode.h"
#include "language_bar.h"
#include <cstring>
#include <shellapi.h>
#include <shlobj.h>

// Slow query thresholds (microseconds)
static constexpr int64_t kSlowIpcUs = 2000;       // IPC round-trip >= 2ms
static constexpr int64_t kSlowWindowUs = 5000;    // candidate window >= 5ms
static constexpr int64_t kSlowTotalUs = 10000;    // PROCESS_KEY total >= 10ms

// Async queue configuration
static constexpr int kTsfQueueCapacity = 128;
static constexpr int kTsfBatchSize = 16;
static constexpr auto kTsfFlushInterval = std::chrono::milliseconds(200);

// Sync ime_status only when server filled valid data (OK or ENGINE_PROCESS_FAILED).
// ERR_INVALID_SESSION means server didn't fill ime_status — don't overwrite local state.
static bool should_sync_ime_status(cxxime::IPCStatus status) {
    return status == cxxime::IPCStatus::OK ||
           status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
}

static const char* tsf_result_str(TextService::TsfResult r) {
    switch (r) {
        case TextService::TsfResult::IPC_FAILED: return "ipc_failed";
        case TextService::TsfResult::COMMITTED:  return "committed";
        case TextService::TsfResult::PREEDIT:    return "preedit";
        case TextService::TsfResult::CLEARED:    return "cleared";
        case TextService::TsfResult::REJECTED:   return "rejected";
        default: return "unknown";
    }
}

int TextService::TsfTrace::to_json(char* buf, int size) const {
    return snprintf(buf, size,
        "{\"vk\":%u,\"mod\":%u,\"result\":\"%s\",\"cands\":%u,\"preedit_len\":%u,"
        "\"total_us\":%lld,\"ipc_us\":%lld,\"window_us\":%lld,\"slow\":%s}",
        vk, modifiers, tsf_result_str(result),
        candidate_count, preedit_len,
        (long long)total_us, (long long)ipc_us, (long long)window_us,
        slow ? "true" : "false");
}

bool TextService::TsfTrace::should_log() const {
    if (result == TsfResult::IPC_FAILED) return true;
    if (slow) return true;
    return false;
}

// ─── Async trace queue (bounded, single writer thread) ───────────────

struct TsfTraceEntry {
    char json[512];
    int len = 0;
};

static TsfTraceEntry g_tsf_queue[kTsfQueueCapacity];
static std::atomic<int> g_tsf_head{0};
static std::atomic<int> g_tsf_tail{0};
static std::atomic<int> g_tsf_dropped{0};

static std::thread g_tsf_writer_thread;
static std::mutex g_tsf_shutdown_mutex;
static std::condition_variable g_tsf_shutdown_cv;
static std::atomic<bool> g_tsf_shutdown{false};
static std::atomic<bool> g_tsf_writer_started{false};

static bool tsf_queue_try_push(const TsfTraceEntry& entry) {
    int head = g_tsf_head.load(std::memory_order_relaxed);
    int next = (head + 1) % kTsfQueueCapacity;
    if (next == g_tsf_tail.load(std::memory_order_acquire)) {
        g_tsf_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_tsf_queue[head] = entry;
    g_tsf_head.store(next, std::memory_order_release);
    return true;
}

static int tsf_queue_pop_batch(TsfTraceEntry* batch, int max) {
    int count = 0;
    while (count < max) {
        int tail = g_tsf_tail.load(std::memory_order_relaxed);
        if (tail == g_tsf_head.load(std::memory_order_acquire))
            break;
        batch[count++] = g_tsf_queue[tail];
        g_tsf_tail.store((tail + 1) % kTsfQueueCapacity, std::memory_order_release);
    }
    return count;
}

static std::string tsf_get_log_dir() {
    wchar_t buf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf)))
        return {};
    std::wstring dir = std::wstring(buf) + L"\\cxxime\\logs";
    CreateDirectoryW((std::wstring(buf) + L"\\cxxime").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    char utf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

static void tsf_rotate_log(FILE*& file, size_t& file_size, const std::string& path) {
    if (file) { fclose(file); file = nullptr; }
    DeleteFileA((path + ".3").c_str());
    MoveFileA((path + ".2").c_str(), (path + ".3").c_str());
    MoveFileA((path + ".1").c_str(), (path + ".2").c_str());
    MoveFileA(path.c_str(), (path + ".1").c_str());
    file_size = 0;
}

static void tsf_writer_thread_func() {
    std::string dir = tsf_get_log_dir();
    if (dir.empty()) return;

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "tsf-%d-trace.jsonl", (int)GetCurrentProcessId());
    std::string path = dir + "\\" + pid_str;

    FILE* file = nullptr;
    size_t file_size = 0;
    static constexpr size_t kMaxFileSize = 64 * 1024 * 1024; // 64 MiB

    file = fopen(path.c_str(), "a");
    if (file) {
        fseek(file, 0, SEEK_END);
        file_size = ftell(file);
    }

    TsfTraceEntry batch[kTsfBatchSize];
    auto last_flush = std::chrono::steady_clock::now();

    while (!g_tsf_shutdown.load(std::memory_order_relaxed)) {
        {
            std::unique_lock<std::mutex> lock(g_tsf_shutdown_mutex);
            g_tsf_shutdown_cv.wait_for(lock, kTsfFlushInterval, [] {
                return g_tsf_shutdown.load(std::memory_order_relaxed);
            });
        }

        int count = tsf_queue_pop_batch(batch, kTsfBatchSize);
        if (count == 0) {
            auto now = std::chrono::steady_clock::now();
            if (file && (now - last_flush) >= kTsfFlushInterval) {
                fflush(file);
                last_flush = now;
            }
            continue;
        }

        if (!file) {
            file = fopen(path.c_str(), "a");
            if (!file) continue;
            fseek(file, 0, SEEK_END);
            file_size = ftell(file);
        }

        for (int i = 0; i < count; ++i) {
            if (file_size + batch[i].len + 1 > kMaxFileSize) {
                tsf_rotate_log(file, file_size, path);
                file = fopen(path.c_str(), "a");
                if (!file) break;
            }
            fwrite(batch[i].json, 1, batch[i].len, file);
            fputc('\n', file);
            file_size += batch[i].len + 1;
        }

        if (file) {
            fflush(file);
            last_flush = std::chrono::steady_clock::now();
        }
    }

    // Final drain on shutdown
    if (file) {
        TsfTraceEntry entry;
        while (tsf_queue_pop_batch(&entry, 1) == 1) {
            if (file_size + entry.len + 1 > kMaxFileSize) {
                tsf_rotate_log(file, file_size, path);
                file = fopen(path.c_str(), "a");
                if (!file) break;
            }
            fwrite(entry.json, 1, entry.len, file);
            fputc('\n', file);
            file_size += entry.len + 1;
        }
        fclose(file);
    }
}

static void tsf_ensure_writer_started() {
    if (g_tsf_writer_started.exchange(true)) return;
    g_tsf_writer_thread = std::thread(tsf_writer_thread_func);
}

void TextService::_enqueue_trace(const TsfTrace& trace) {
    if (!trace.should_log()) return;

    TsfTraceEntry entry;
    entry.len = trace.to_json(entry.json, sizeof(entry.json));
    if (entry.len <= 0) return;

    tsf_ensure_writer_started();
    tsf_queue_try_push(entry);  // Drop if full — never block hot path
}

// ─── TextService lifecycle ───────────────────────────────────────────

TextService::TextService() {}

TextService::~TextService() {}

// Called from DllMain(DLL_PROCESS_DETACH) via globals.cpp
void TextService::shutdown_trace() {
    if (!g_tsf_writer_started.exchange(false))
        return;
    g_tsf_shutdown.store(true, std::memory_order_relaxed);
    g_tsf_shutdown_cv.notify_all();
    if (g_tsf_writer_thread.joinable())
        g_tsf_writer_thread.join();
}

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
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
        *ppvObj = static_cast<ITfThreadMgrEventSink*>(this);
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

void TextService::_sync_ime_status(const cxxime::ImeStatus& status) {
    bool chinese_changed = (_chinese_mode != status.chinese_mode);
    bool caps_changed = (_caps_lock != status.caps_lock);
    _chinese_mode = status.chinese_mode;
    _caps_lock = status.caps_lock;

    _sync_conversion_mode_compartment(status);
    if (_statusController.is_initialized()) _statusController.sync_status(status);
    if (_modeButton) _modeButton->update_from_status(status);
    if (_imeButton) _imeButton->update_mode(status.input_mode);

    if (chinese_changed || caps_changed) _refresh_mode_button_item();
}

void TextService::_sync_conversion_mode_compartment(const cxxime::ImeStatus& status) {
    if (!_threadMgr || _clientId == TF_CLIENTID_NULL) return;

    ITfCompartmentMgr* compartment_mgr = nullptr;
    HRESULT hr = _threadMgr->QueryInterface(IID_ITfCompartmentMgr,
                                            reinterpret_cast<void**>(&compartment_mgr));

    if (FAILED(hr) || !compartment_mgr) return;

    ITfCompartment* compartment = nullptr;
    hr = compartment_mgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                         &compartment);
    compartment_mgr->Release();

    if (FAILED(hr) || !compartment) return;

    DWORD conversion_mode = 0;
    VARIANT current = {};
    VariantInit(&current);
    if (SUCCEEDED(compartment->GetValue(&current))) {
        if (current.vt == VT_I4 || current.vt == VT_INT) {
            conversion_mode = static_cast<DWORD>(current.lVal);
        } else if (current.vt == VT_UI4 || current.vt == VT_UINT) {
            conversion_mode = current.ulVal;
        }
    }
    VariantClear(&current);

    DWORD new_mode = conversion_mode;
    if (status.chinese_mode) {
        new_mode |= TF_CONVERSIONMODE_NATIVE;
    } else {
        new_mode &= ~TF_CONVERSIONMODE_NATIVE;
    }

    if (new_mode != conversion_mode) {
        VARIANT next = {};
        VariantInit(&next);
        next.vt = VT_I4;
        next.lVal = static_cast<LONG>(new_mode);
        hr = compartment->SetValue(_clientId, &next);
        CXXIME_LOG(L"sync_conversion_mode: chinese=%d, mode=0x%08x->0x%08x, hr=0x%08x",
                   status.chinese_mode ? 1 : 0, conversion_mode, new_mode, hr);
        VariantClear(&next);
    }
    compartment->Release();
}

void TextService::_refresh_mode_button_item() {
    if (!_threadMgr || !_modeButton) return;

    ITfLangBarItemMgr* item_mgr = nullptr;
    HRESULT hr_qi =
        _threadMgr->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&item_mgr));

    if (FAILED(hr_qi) || !item_mgr) {
        CXXIME_LOG(L"ModeButton refresh: QI ITfLangbarItemMgr failed, hr=0x%08x", hr_qi);
        return;
    }

    HRESULT hr_remove = item_mgr->RemoveItem(_modeButton);
    HRESULT hr_add = item_mgr->AddItem(_modeButton);
    CXXIME_LOG(L"ModeButton refresh: remove=0x%08x, add=0x%08x", hr_remove, hr_add);
    item_mgr->Release();
}

// ITfTextInputProcessorEx
STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    OutputDebugStringA("[CxxIME] ActivateEx called\n");
    CXXIME_LOG(L"ActivateEx: clientId=%u, flags=%u", tid, dwFlags);

    _config = get_config();
    init_config_monitor();
    add_config_monitor_ref();

    _threadMgr = ptim;
    _threadMgr->AddRef();
    _clientId = tid;
    _activateFlags = dwFlags;

    _register_key_event_sink();
    _register_preserved_key();

    // Register thread focus sink to detect window/app switches
    {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfSource, (void**)&pSource))) {
            pSource->AdviseSink(IID_ITfThreadFocusSink,
                                static_cast<ITfThreadFocusSink*>(this), &_dwThreadFocusCookie);
            pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                                static_cast<ITfThreadMgrEventSink*>(this), &_dwThreadMgrEventCookie);
            pSource->Release();
        }
    }

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

    // Register language bar buttons
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        _modeButton = new CLangBarItemButton(tid, GUID_LBI_INPUTMODE);
        _imeButton = new CLangBarImeButton(tid, c_guidLangBarImeButton);

        if (FAILED(pLangBarItemMgr->AddItem(_modeButton))) {
            CXXIME_LOG(L"Failed to add mode button to language bar");
        }
        if (FAILED(pLangBarItemMgr->AddItem(_imeButton))) {
            CXXIME_LOG(L"Failed to add IME button to language bar");
        }

        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Language bar buttons registered");
    } else {
        CXXIME_LOG(L"Failed to get ITfLangBarItemMgr interface");
    }

    // Initialize status window controller
    if (_config.status_window.enable) {
        bool first_init = !_statusController.is_initialized();
        if (!_statusController.initialize(nullptr, &_client, _sessionId, &_config)) {
            CXXIME_LOG(L"StatusController: window creation failed, disabled");
        } else {
            _statusController.update_config(_config);
            if (first_init && _config.status_window.show_on_startup) {
                _statusController.show();
            }
        }
        // Set language bar callback for "显示/隐藏状态栏"
        if (_modeButton) {
            _modeButton->set_show_status_callback([this]() {
                _config.status_window.enable = !_config.status_window.enable;
                _statusController.update_config(_config);
                _config.save(cxxime::user_data_path("default.json"));
                if (_config.status_window.enable) {
                    _statusController.show();
                } else {
                    _statusController.hide();
                }
                _modeButton->set_status_visible(_config.status_window.enable);
            });
        }

        // Set menu callback for left-click IPC toggle
        if (_modeButton) {
            _modeButton->set_menu_callback([this](int menu_id) {
                CXXIME_LOG(L"menu_callback: menu_id=%d, sessionId=%u", menu_id, _sessionId);
                cxxime::IPCResponse resp = {};
                _client.toggle_chinese(_sessionId, resp);
                CXXIME_LOG(L"menu_callback: toggle_chinese result status=%d, chinese=%d",
                           (int)resp.status, resp.ime_status.chinese_mode);
                if (resp.status == cxxime::IPCStatus::OK) {
                    _sync_ime_status(resp.ime_status);
                }
            });

            // Set toggle input mode callback (拼音/五笔切换)
            _modeButton->set_toggle_input_mode_callback([this]() {
                CXXIME_LOG(L"toggle_input_mode_callback: sessionId=%u", _sessionId);
                cxxime::IPCResponse resp = {};
                _client.switch_input_mode(_sessionId, resp);
                if (resp.status == cxxime::IPCStatus::OK) {
                    _sync_ime_status(resp.ime_status);
                }
            });

            // Set open settings callback
            _modeButton->set_open_settings_callback([]() {
                HWND existing = FindWindowW(nullptr, L"CxxIME 设置");
                if (existing) {
                    SetForegroundWindow(existing);
                    return;
                }
                wchar_t dll_path[MAX_PATH] = {};
                GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);
                wchar_t* last_slash = wcsrchr(dll_path, L'\\');
                if (last_slash) *(last_slash + 1) = L'\0';
                std::wstring settings_path = std::wstring(dll_path) + L"cxxime-settings.exe";
                STARTUPINFOW si = {};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi = {};
                CreateProcessW(settings_path.c_str(), nullptr, nullptr, nullptr,
                               FALSE, 0, nullptr, nullptr, &si, &pi);
                if (pi.hProcess) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
            });

            // Set about callback
            _modeButton->set_about_callback([]() {
                MessageBoxW(nullptr,
                            L"CxxIME 轻量级输入法\n"
                            L"版本: 1.0.0\n\n"
                            L"© 2026 CxxIME Contributors\n"
                            L"Apache License 2.0",
                            L"关于 CxxIME",
                            MB_OK | MB_ICONINFORMATION);
            });

            // Set switch input mode callback (纯拼音/纯五笔/混输)
            _modeButton->set_switch_input_mode_callback([this](int mode) {
                CXXIME_LOG(L"switch_input_mode_callback: mode=%d, sessionId=%u", mode, _sessionId);
                cxxime::IPCResponse resp = {};
                _client.switch_input_mode(_sessionId, static_cast<cxxime::InputMode>(mode), resp);
                if (resp.status == cxxime::IPCStatus::OK) {
                    _sync_ime_status(resp.ime_status);
                }
            });

            // Set quick phrase callback (快捷造词)
            _modeButton->set_quick_phrase_callback([this]() {
                CXXIME_LOG(L"quick_phrase_callback: sessionId=%u", _sessionId);
                HWND existing = FindWindowW(nullptr, L"CxxIME 设置");
                if (existing) {
                    // TODO: send message to switch to dictionary panel
                    SetForegroundWindow(existing);
                    return;
                }
                wchar_t dll_path[MAX_PATH] = {};
                GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);
                wchar_t* last_slash = wcsrchr(dll_path, L'\\');
                if (last_slash) *(last_slash + 1) = L'\0';
                std::wstring settings_path = std::wstring(dll_path) + L"cxxime-settings.exe";
                std::wstring cmd_line = L"\"" + settings_path + L"\" --quick-phrase";
                STARTUPINFOW si = {};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi = {};
                CreateProcessW(nullptr, &cmd_line[0], nullptr, nullptr,
                               FALSE, 0, nullptr, nullptr, &si, &pi);
                if (pi.hProcess) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
            });

            // Set status visible state
            _modeButton->set_status_visible(_config.status_window.enable);
        }
    }

    cxxime::IPCResponse resp = {};
    if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(resp.ime_status);
    }
    _activated = true;
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    CXXIME_LOG(L"Deactivate: sessionId=%u", _sessionId);
    _activated = false;

    // Hide status window immediately, then destroy — avoid clicks during IPC teardown
    _statusController.hide();
    _statusController.shutdown();

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

    release_config_monitor_ref();

    _candidateWindow.destroy();

    // Unregister language bar buttons
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (_threadMgr && SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        if (_modeButton) {
            pLangBarItemMgr->RemoveItem(_modeButton);
            _modeButton->Release();
            _modeButton = nullptr;
        }
        if (_imeButton) {
            pLangBarItemMgr->RemoveItem(_imeButton);
            _imeButton->Release();
            _imeButton = nullptr;
        }
        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Language bar buttons unregistered");
    }

    // Unregister thread focus sink and event sink
    if (_threadMgr) {
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfSource, (void**)&pSource))) {
            if (_dwThreadFocusCookie != TF_INVALID_COOKIE)
                pSource->UnadviseSink(_dwThreadFocusCookie);
            if (_dwThreadMgrEventCookie != TF_INVALID_COOKIE)
                pSource->UnadviseSink(_dwThreadMgrEventCookie);
            pSource->Release();
        }
        _dwThreadFocusCookie = TF_INVALID_COOKIE;
        _dwThreadMgrEventCookie = TF_INVALID_COOKIE;
    }

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
    if (fForeground) {
        _client.focus_in(_sessionId);
    } else {
        _client.focus_out(_sessionId);
        _AbortComposition();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyDownPending = true;
    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);

    // Modifier keys (Shift/Ctrl/Alt) must be eaten so TSF calls OnKeyDown,
    // which sends the key event to the server via IPC. Without this, TSF
    // passes the key directly to the app and OnKeyDown is never called.
    if (wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_SHIFT ||
        wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == VK_CONTROL ||
        wParam == VK_LMENU || wParam == VK_RMENU ||
        wParam == VK_LWIN || wParam == VK_RWIN) {
        *pfEaten = TRUE;
    }

    OutputDebugStringA("[CxxIME] OnTestKeyDown\n");
    CXXIME_LOG(L"OnTestKeyDown: vk=%u, eaten=%d, sessionId=%u", (unsigned int)wParam, *pfEaten, _sessionId);
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyUpPending = true;
    *pfEaten = FALSE;
    CXXIME_LOG(L"OnTestKeyUp: vk=%u, sessionId=%u", (unsigned int)wParam, _sessionId);
    _ProcessKeyUp(wParam);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (_fTestKeyDownPending) {
        _fTestKeyDownPending = false;
        // OnTestKeyDown already sent the key to the server.
        // Re-read *pfEaten — it was set by _ProcessKeyEvent called from OnTestKeyDown.
        // Don't override it here; the value is already in *pfEaten from the OnTestKeyDown call.
        return S_OK;
    }
    // Some apps call OnKeyDown without OnTestKeyDown (e.g. QQ2012)
    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);
    return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (_fTestKeyUpPending) {
        _fTestKeyUpPending = false;
        *pfEaten = FALSE;
        return S_OK;
    }
    // Some apps call OnKeyUp without OnTestKeyUp
    _ProcessKeyUp(wParam);
    *pfEaten = FALSE;
    return S_OK;
}

bool TextService::_ProcessKeyEvent(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;

    // Config is reloaded by watcher thread (not keypress-driven).
    // Copy to local _config for consistent use during this keypress.
    _config = get_config();

    // Record key event start time
    _key_event_start = std::chrono::steady_clock::now();

    uint32_t modifiers = _get_modifiers();
    CXXIME_LOG(L"_ProcessKeyEvent: vk=%u, mods=%u, composing=%d", (unsigned int)wParam, modifiers, _composing);

    cxxime::IPCResponse response = {};
    auto ipc_start = std::chrono::steady_clock::now();
    bool ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
    auto ipc_end = std::chrono::steady_clock::now();
    _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();

    // If IPC failed, try to reconnect and re-create session
    if (!ok) {
        if (_client.connect()) {
            _client.start_session(_sessionId);
            CXXIME_LOG(L"Reconnected, new sessionId=%u", _sessionId);
            ipc_start = std::chrono::steady_clock::now();
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
            ipc_end = std::chrono::steady_clock::now();
            _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();
        }
    }

    // Build trace (populated at all exit paths)
    TsfTrace trace;
    trace.vk = (uint32_t)wParam;
    trace.modifiers = modifiers;
    trace.ipc_us = _last_ipc_us;

    if (!ok) {
        CXXIME_LOG(L"_ProcessKeyEvent: IPC FAILED for vk=%u, sessionId=%u", (unsigned int)wParam, _sessionId);
        trace.result = TsfResult::IPC_FAILED;
        auto total_end = std::chrono::steady_clock::now();
        trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - _key_event_start).count();
        trace.slow = (trace.ipc_us >= kSlowIpcUs) || (trace.total_us >= kSlowTotalUs);
        _enqueue_trace(trace);
        return false;
    }

    CXXIME_LOG(L"_ProcessKeyEvent: ok, vk=%u, ascii=%d, commit='%S', preedit='%S', composing=%d",
               (unsigned int)wParam, response.ascii_mode, response.commit_text, response.preedit, response.composing);

    // Sync mode state from engine — only when server filled valid ime_status
    if (should_sync_ime_status(response.status)) {
        _sync_ime_status(response.ime_status);
    }

    // Handle committed text (e.g. Shift toggle with commit_text, or normal candidate selection)
    if (response.commit_text[0] != '\0') {
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
        trace.result = TsfResult::COMMITTED;
        trace.candidate_count = response.candidate_count;
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

        CXXIME_LOG(L"_ProcessKeyEvent: start_comp=%d, _composing=%d, _composition=%d, inline='%s'",
                   decision.start_composition, _composing, _composition != nullptr,
                   decision.inline_text.c_str());

        if (decision.start_composition) {
            if (!_composing) _start_composition(pic);
            update_composition(pic, decision.inline_text);
        } else {
            if (_composing && _composition) _end_composition(pic);
            _composing = true;
            update_composition(pic, L"");
        }
        *pfEaten = TRUE;

        std::string popup_preedit = decision.show_preedit_in_popup ? response.preedit : "";
        _candidateWindow.set_preedit(popup_preedit);

        bool has_candidates = response.candidate_count > 0;
        bool has_preedit = !popup_preedit.empty();

        CXXIME_LOG(L"_ProcessKeyEvent: has_cand=%d, has_preedit=%d, cand_count=%u",
                   has_candidates, has_preedit, response.candidate_count);

        auto window_start = std::chrono::steady_clock::now();

        if (has_candidates || has_preedit) {
            if (has_candidates) {
                cxxime::CandidatePage page;
                page.highlighted = (int)response.highlighted;
                for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                    cxxime::Candidate c;
                    c.text = response.candidates[i];
                    page.candidates.push_back(std::move(c));
                }
                _candidateWindow.update(page);
            } else {
                _candidateWindow.update({});
            }

            // Query caret position via synchronous TSF edit session
            RECT caretRect = {};
            bool caretResolved = false;
            EditSession* pCaretSession = new (std::nothrow) EditSession(this, pic);
            if (pCaretSession) {
                pCaretSession->set_action(EditSession::Action::QUERY_CARET);
                HRESULT hr = E_FAIL;
                pic->RequestEditSession(_clientId, pCaretSession,
                                        TF_ES_READ | TF_ES_SYNC, &hr);
                if (SUCCEEDED(hr))
                    caretResolved = pCaretSession->get_caret_rect(caretRect);
                pCaretSession->Release();
            }
            if (!caretResolved)
                caretRect = _resolve_caret_rect(pic);

            _candidateWindow.move_to_caret(caretRect);
            _candidateWindow.show();
        } else {
            _candidateWindow.hide();
        }

        auto window_end = std::chrono::steady_clock::now();
        _last_window_update_us = std::chrono::duration_cast<std::chrono::microseconds>(window_end - window_start).count();

        trace.result = TsfResult::PREEDIT;
        trace.candidate_count = response.candidate_count;
        trace.preedit_len = (uint32_t)strlen(response.preedit);
        trace.window_us = _last_window_update_us;

        CXXIME_LOG(L"_ProcessKeyEvent: window_us=%lld, ipc_us=%lld", _last_window_update_us, _last_ipc_us);
    } else if (response.status == cxxime::IPCStatus::OK) {
        // Server accepted but no commit and no preedit (e.g. Escape cleared the buffer)
        _candidateWindow.hide();
        _candidateWindow.set_preedit("");
        _end_composition(pic);
        _composing = false;
        *pfEaten = TRUE;
        trace.result = TsfResult::CLEARED;
    } else {
        trace.result = TsfResult::REJECTED;
    }

    // Finalize and enqueue trace (async, non-blocking)
    auto total_end = std::chrono::steady_clock::now();
    trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - _key_event_start).count();
    trace.slow = (trace.ipc_us >= kSlowIpcUs) || (trace.window_us >= kSlowWindowUs) || (trace.total_us >= kSlowTotalUs);
    _enqueue_trace(trace);

    return *pfEaten != FALSE;
}

void TextService::_ProcessKeyUp(WPARAM wParam) {
    // Config is reloaded by watcher thread (not keypress-driven).
    _config = get_config();

    uint32_t modifiers = _get_modifiers();
    CXXIME_LOG(L"_ProcessKeyUp: vk=%u, mods=%u, sessionId=%u", (unsigned int)wParam, modifiers,
               _sessionId);

    cxxime::IPCResponse response = {};
    bool ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response, true);

    // If IPC failed, try to reconnect and re-create session
    if (!ok) {
        CXXIME_LOG(L"_ProcessKeyUp: IPC failed, attempting reconnect");
        if (_client.connect()) {
            _client.start_session(_sessionId);
            CXXIME_LOG(L"_ProcessKeyUp: Reconnected, new sessionId=%u", _sessionId);
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response, true);
        }
    }

    CXXIME_LOG(L"_ProcessKeyUp: ok=%d, ascii_mode=%d, commit='%S', composing=%d",
               ok, response.ascii_mode, response.commit_text, response.composing);

    if (ok) {
        if (response.status == cxxime::IPCStatus::OK ||
            response.status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED) {
            _sync_ime_status(response.ime_status);
        }
        CXXIME_LOG(L"_ProcessKeyUp: _chinese_mode=%d, _composing=%d", _chinese_mode, _composing);

        // Handle committed text from toggle (e.g. Shift with commit_text style)
        if (response.commit_text[0] != '\0') {
            std::wstring commit_text;
            int len = MultiByteToWideChar(CP_UTF8, 0, response.commit_text, -1, nullptr, 0);
            if (len > 0) {
                commit_text.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, response.commit_text, -1, &commit_text[0], len);
            }
            if (!commit_text.empty()) {
                insert_text(commit_text);
                ITfDocumentMgr* pDocMgr = nullptr;
                if (_threadMgr && SUCCEEDED(_threadMgr->GetFocus(&pDocMgr)) && pDocMgr) {
                    ITfContext* pContext = nullptr;
                    if (SUCCEEDED(pDocMgr->GetBase(&pContext)) && pContext) {
                        _end_composition(pContext);
                        pContext->Release();
                    }
                    pDocMgr->Release();
                }
                _composing = false;
                _candidateWindow.hide();
                _candidateWindow.set_preedit("");
            }
        }
    }
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (IsEqualGUID(rguid, c_guidPreservedKey_Toggle) && !_composing) {
        //_chinese_mode = !_chinese_mode;
        cxxime::IPCResponse resp = {};
        if (_client.toggle_chinese(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
            _sync_ime_status(resp.ime_status);
        }
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
    if (_activated && _statusController.is_initialized())
        _statusController.show();
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    if (_statusController.is_initialized())
        _statusController.hide();
    _client.focus_out(_sessionId);
    _AbortComposition();
    return S_OK;
}

void TextService::_AbortComposition() {
    _candidateWindow.hide();
    _candidateWindow.set_preedit("");
    if (_composing) {
        ITfDocumentMgr* pDocMgr = nullptr;
        if (_threadMgr && SUCCEEDED(_threadMgr->GetFocus(&pDocMgr)) && pDocMgr) {
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

// ITfThreadMgrEventSink
STDMETHODIMP TextService::OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) {
    return S_OK;
}

STDMETHODIMP TextService::OnSetFocus(ITfDocumentMgr* pDocMgrFocus, ITfDocumentMgr* pDocMgrPrevFocus) {
    // Sync status on focus change (user may have toggled via language bar)
    //if (_statusController.is_initialized()) {
    //    cxxime::IPCResponse resp = {};
    //    if (_client.get_status(_sessionId, resp)) {
    //        _statusController.sync_status(resp.ime_status);
    //        _chinese_mode = resp.ime_status.chinese_mode;
    //        if (_modeButton)
    //            _modeButton->update_from_status(resp.ime_status);
    //        if (_imeButton) _imeButton->update_mode(resp.ime_status.input_mode);
    //    }
    //}
    cxxime::IPCResponse resp = {};
    if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(resp.ime_status);
    }

    // Document focus changed — hide candidate window if switching away
    if (_composing) {
        _candidateWindow.hide();
        _candidateWindow.set_preedit("");
        _client.focus_out(_sessionId);
        // End composition in the previous context
        if (pDocMgrPrevFocus) {
            ITfContext* pContext = nullptr;
            if (SUCCEEDED(pDocMgrPrevFocus->GetBase(&pContext)) && pContext) {
                _end_composition(pContext);
                pContext->Release();
            }
        }
        _composing = false;
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext* pic) {
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
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
    if (!pic)
        return;

    EditSession* pSession = new (std::nothrow) EditSession(this, pic);
    if (!pSession)
        return;

    pSession->set_action(EditSession::Action::UPDATE_COMPOSITION, preedit);

    HRESULT hr = E_FAIL;
    pic->RequestEditSession(_clientId, pSession, TF_ES_READWRITE | TF_ES_ASYNCDONTCARE, &hr);

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
        if (kb[VK_CAPITAL] & 0x1)
            mods |= 0x08;
    }
    return mods;
}

RECT TextService::_resolve_caret_rect(ITfContext* pic) {
    if (_caretRect.left != 0 || _caretRect.right != 0 ||
        _caretRect.top != 0 || _caretRect.bottom != 0) {
        return _caretRect;
    }

    RECT rc = {};

    GUITHREADINFO gti = { sizeof(gti) };
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret) {
        POINT pt = { gti.rcCaret.left, gti.rcCaret.top };
        ClientToScreen(gti.hwndCaret, &pt);
        SetRect(&rc, pt.x, pt.y, pt.x, pt.y + 20);
        return rc;
    }

    POINT pt = {};
    if (GetCaretPos(&pt)) {
        HWND focus = GetFocus();
        if (focus) ClientToScreen(focus, &pt);
        SetRect(&rc, pt.x, pt.y, pt.x, pt.y + 20);
        return rc;
    }

    return rc;
}
