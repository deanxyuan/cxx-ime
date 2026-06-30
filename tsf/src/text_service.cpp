// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"
#include "globals.h"
#include "edit_session.h"
#include "display_attribute.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <cxxime/diagnostics_config.h>
#include <cxxime/render_context.h>
#include "preedit_mode.h"
#include "language_bar.h"
#include "about_dialog.h"
#include <atomic>
#include <cstring>
#include <shellapi.h>
#include <shlobj.h>
#include <unordered_map>

static constexpr auto kIpcHeartbeatInterval = std::chrono::milliseconds(1500);
static constexpr int kTsfIpcTimeoutMs = 800;

// Async queue configuration
static constexpr int kTsfQueueCapacity = 128;
static constexpr int kTsfBatchSize = 16;
static constexpr auto kTsfFlushInterval = std::chrono::milliseconds(200);

// Sync ime_status only when server filled valid data (OK or ENGINE_PROCESS_FAILED).
// ERR_INVALID_SESSION means server did not fill ime_status; keep local state.
static bool should_sync_ime_status(cxxime::IPCStatus status) {
    return status == cxxime::IPCStatus::OK ||
           status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED;
}

static bool same_visible_status(const cxxime::ImeStatus& a, const cxxime::ImeStatus& b) {
    return a.chinese_mode == b.chinese_mode &&
           a.caps_lock == b.caps_lock &&
           a.full_shape == b.full_shape &&
           a.chinese_punct == b.chinese_punct &&
           a.input_mode == b.input_mode;
}

static std::mutex g_last_status_mutex;
static bool g_has_last_status = false;
static cxxime::ImeStatus g_last_status;

static constexpr UINT kStatePollIntervalMs = 30;
static std::mutex g_state_poll_timer_mutex;
static std::unordered_map<UINT_PTR, TextService*> g_state_poll_timers;

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
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (result == TsfResult::IPC_FAILED) return true;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kVerbose)
        return true;
    if (slow) return true;
    return false;
}

// Async trace queue (bounded, single writer thread)

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

static void tsf_rotate_log(FILE*& file, size_t& file_size, const std::string& path,
                           const cxxime::DiagnosticsConfig& config) {
    if (file) { fclose(file); file = nullptr; }
    DeleteFileA((path + "." + std::to_string(config.log_max_files)).c_str());
    for (int i = config.log_max_files - 1; i >= 1; --i) {
        MoveFileA((path + "." + std::to_string(i)).c_str(),
                  (path + "." + std::to_string(i + 1)).c_str());
    }
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
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + batch[i].len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
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
            cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
            if (file_size + entry.len + 1 > config.log_max_size) {
                tsf_rotate_log(file, file_size, path, config);
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
    tsf_queue_try_push(entry);  // Drop if full; never block hot path.
}

// TextService lifecycle

TextService::TextService() {}

TextService::~TextService() {
    _stop_state_poll_timer();
}

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
    bool local_changed = _chinese_mode != status.chinese_mode ||
                         _caps_lock != status.caps_lock;
    bool visible_changed = true;
    _chinese_mode = status.chinese_mode;
    _caps_lock = status.caps_lock;
    {
        std::lock_guard<std::mutex> lock(g_last_status_mutex);
        visible_changed = !g_has_last_status || !same_visible_status(g_last_status, status);
        g_last_status = status;
        g_has_last_status = true;
    }
    if (!local_changed && !visible_changed)
        return;

    // Update button state before notifying the compartment. The language bar
    // queries GetIcon during the notification, so stale button state causes a
    // second refresh and visible flicker.
    if (_modeButton) _modeButton->update_from_status(status);
    if (_statusController.is_initialized()) _statusController.sync_status(status);
    _sync_conversion_mode_compartment(status);
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

bool TextService::_is_caps_lock_on(bool allow_recent_hint) const {
    BYTE kb[256] = {};
	if (GetKeyboardState(kb) && (kb[VK_CAPITAL] & 0x01))
        return true;

    if (GetKeyState(VK_CAPITAL) & 0x0001)
        return true;

    // Activation can run before the target thread has consumed any keyboard
    // message. Use the recent physical press bit only as an initial hint; the
    // first real key event will correct the state through its modifier flags.
    return allow_recent_hint && (GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0;
}

bool TextService::_foreground_allows_input() const {
	HWND foreground = GetForegroundWindow();
	if (!foreground)
		return false;

	wchar_t class_name[64] = {};
	GetClassNameW(foreground, class_name, ARRAYSIZE(class_name));
	if (wcscmp(class_name, L"Progman") == 0 ||
		wcscmp(class_name, L"WorkerW") == 0 ||
		wcscmp(class_name, L"Shell_TrayWnd") == 0) {
		return false;
	}

	return true;
}

bool TextService::_context_allows_input(ITfContext* context) const {
	if (!context)
		return false;
	if (!_foreground_allows_input())
		return false;

	TF_STATUS status = {};
	if (FAILED(context->GetStatus(&status)))
		return true;

	return (status.dwDynamicFlags & TF_SD_READONLY) == 0;
}

bool TextService::_document_allows_input(ITfDocumentMgr* doc_mgr) const {
	if (!doc_mgr)
		return false;

	ITfContext* context = nullptr;
	HRESULT hr = doc_mgr->GetBase(&context);
	if (FAILED(hr) || !context)
		return false;

	bool allowed = _context_allows_input(context);
	context->Release();
	return allowed;
}

bool TextService::_query_input_focus_from_thread_mgr() const {
	bool focused = false;
	if (_threadMgr) {
		ITfDocumentMgr* doc_mgr = nullptr;
		if (SUCCEEDED(_threadMgr->GetFocus(&doc_mgr)) && doc_mgr) {
			focused = _document_allows_input(doc_mgr);
			doc_mgr->Release();
		}
	}

	return focused;
}

bool TextService::_update_input_focus_from_thread_mgr() {
	bool focused = _query_input_focus_from_thread_mgr();

    _inputFocused = focused;
    if (focused) {
    // Keep the poll timer alive while activated. It is cheap and only acts
    // when the foreground is not an editable context, covering desktop
    // clicks where TSF may not send focus/key callbacks.
        _start_state_poll_timer();
    } else {
        _start_state_poll_timer();
    }
	if (!focused && _statusController.is_initialized())
		_statusController.hide();
	return focused;
}

bool TextService::_ensure_ipc_session() {
    if (_sessionId && _client.is_connected())
        return true;

    if (!_client.is_connected() &&
        !_client.connect(cxxime::IPC_PIPE_BASE_NAME, kTsfIpcTimeoutMs)) {
        if (_ipcHealthy) {
            CXXIME_LOG(L"IPC unavailable");
        }
        _ipcHealthy = false;
        return false;
    }

    uint32_t session_id = 0;
    if (!_client.start_session(session_id) || session_id == 0) {
        CXXIME_LOG(L"Failed to start IPC session");
        _sessionId = 0;
        _ipcHealthy = false;
        _client.disconnect();
        return false;
    }

    _sessionId = session_id;
    _ipcHealthy = true;
    _lastIpcHeartbeat = std::chrono::steady_clock::now();
    CXXIME_LOG(L"IPC session ready, sessionId=%u", _sessionId);
    return true;
}

bool TextService::_recreate_ipc_session_preserving_status() {
    cxxime::ImeStatus desired_status = {};
    bool has_desired_status = false;
    {
        std::lock_guard<std::mutex> lock(g_last_status_mutex);
        has_desired_status = g_has_last_status;
        if (has_desired_status)
            desired_status = g_last_status;
    }
    bool desired_chinese_mode = has_desired_status ? desired_status.chinese_mode : _chinese_mode;
    bool physical_caps_lock = _is_caps_lock_on(false);

    _sessionId = 0;
    _ipcHealthy = false;
    if (!_ensure_ipc_session())
        return false;

    cxxime::ImeStatus synced_status = {};
    if (!_sync_caps_lock_state(physical_caps_lock, &synced_status))
        return false;

    if (!physical_caps_lock && synced_status.chinese_mode != desired_chinese_mode) {
        cxxime::IPCResponse toggle_resp = {};
        if (_client.toggle_chinese(_sessionId, toggle_resp) &&
            toggle_resp.status == cxxime::IPCStatus::OK) {
            _sync_ime_status(toggle_resp.ime_status);
        } else {
            return false;
        }
    }
    if (has_desired_status && synced_status.input_mode != desired_status.input_mode) {
        cxxime::IPCResponse mode_resp = {};
        if (_client.switch_input_mode(_sessionId, desired_status.input_mode, mode_resp) &&
            mode_resp.status == cxxime::IPCStatus::OK) {
            _sync_ime_status(mode_resp.ime_status);
        } else {
            return false;
        }
    }
    return true;
}

bool TextService::_heartbeat_ipc() {
    if (!_activated || !_sessionId)
        return false;

    auto now = std::chrono::steady_clock::now();
    if (_lastIpcHeartbeat.time_since_epoch().count() != 0 &&
        now - _lastIpcHeartbeat < kIpcHeartbeatInterval) {
        return _ipcHealthy;
    }
    _lastIpcHeartbeat = now;

    if (!_client.ensure_connected()) {
        CXXIME_LOG(L"IPC heartbeat reconnect failed");
        _sessionId = 0;
        _ipcHealthy = false;
        return false;
    }

    cxxime::IPCResponse resp = {};
    if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _ipcHealthy = true;
        _sync_ime_status(resp.ime_status);
        return true;
    }
    if (resp.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        CXXIME_LOG(L"IPC heartbeat detected invalid session, recreating");
        return _recreate_ipc_session_preserving_status();
    }

    CXXIME_LOG(L"IPC heartbeat failed, reconnecting");
    _client.disconnect();
    _sessionId = 0;
    _ipcHealthy = false;
    return false;
}

bool TextService::_sync_caps_lock_state(bool caps_lock, cxxime::ImeStatus* synced_status) {
    if (!_ensure_ipc_session())
        return false;

    cxxime::IPCResponse resp = {};
    if (_client.sync_caps_lock(_sessionId, caps_lock, resp) &&
        resp.status == cxxime::IPCStatus::OK) {
        if (synced_status)
            *synced_status = resp.ime_status;
        _sync_ime_status(resp.ime_status);
        return true;
    }
    return false;
}

bool TextService::_sync_physical_caps_lock(cxxime::ImeStatus* synced_status) {
    bool caps_lock = _is_caps_lock_on();
    if (!_seenKeyAfterActivate && _caps_lock && !caps_lock)
        return false;

    return _sync_caps_lock_state(caps_lock, synced_status);
}

void TextService::_reset_poll_shift_state() {
    bool left_shift_down = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    bool right_shift_down = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    bool generic_shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    _pollShiftDown = left_shift_down || right_shift_down || generic_shift_down;
    _pollShiftKey = _pollShiftDown
        ? (right_shift_down ? VK_RSHIFT : (left_shift_down ? VK_LSHIFT : VK_SHIFT))
        : VK_SHIFT;
}

void TextService::_start_state_poll_timer() {
    if (_statePollTimer || !_activated)
        return;

    UINT_PTR timer = SetTimer(nullptr, 0, kStatePollIntervalMs, _state_poll_timer_proc);
    if (!timer)
        return;

    {
        std::lock_guard<std::mutex> lock(g_state_poll_timer_mutex);
        g_state_poll_timers[timer] = this;
    }
    _statePollTimer = timer;
    _reset_poll_shift_state();
}

void TextService::_stop_state_poll_timer() {
    if (!_statePollTimer)
        return;

    UINT_PTR timer = _statePollTimer;
    _statePollTimer = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_poll_timer_mutex);
        g_state_poll_timers.erase(timer);
    }
    KillTimer(nullptr, timer);
    _pollShiftDown = false;
    _pollShiftKey = VK_SHIFT;
}

VOID CALLBACK TextService::_state_poll_timer_proc(HWND, UINT, UINT_PTR id_event, DWORD) {
    TextService* service = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_state_poll_timer_mutex);
        auto it = g_state_poll_timers.find(id_event);
        if (it != g_state_poll_timers.end())
            service = it->second;
    }
    if (service)
        service->_poll_unfocused_state_keys();
}

bool TextService::_sync_status_key_edge(WPARAM key, bool key_down) {
    if (!_ensure_ipc_session())
        return false;

    uint32_t modifiers = 0;
    if (key_down)
        modifiers |= 0x01;
    if (_caps_lock)
        modifiers |= 0x08;

    cxxime::IPCResponse response = {};
    bool ok = _client.process_key(_sessionId, static_cast<uint32_t>(key), modifiers, response, !key_down);
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = _client.process_key(_sessionId, static_cast<uint32_t>(key), modifiers, response, !key_down);
        }
    }
    if (!ok) {
        _client.disconnect();
        _sessionId = 0;
    }
    if (!ok && _recreate_ipc_session_preserving_status()) {
        response = {};
        ok = _client.process_key(_sessionId, static_cast<uint32_t>(key), modifiers, response, !key_down);
    }

    if (ok && should_sync_ime_status(response.status))
        _sync_ime_status(response.ime_status);
    return ok;
}

void TextService::_poll_unfocused_state_keys() {
    if (!_activated)
        return;

    _heartbeat_ipc();
    if (!_sessionId)
        return;

    bool focused = _query_input_focus_from_thread_mgr();
    if (focused) {
        if (!_inputFocused) {
            _inputFocused = true;
            _reset_poll_shift_state();
            _show_status_window_if_allowed();
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        }
        return;
    }

    if (_inputFocused) {
        _inputFocused = false;
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        _AbortComposition();
        _reset_poll_shift_state();
    }
    if (_statusController.is_initialized())
        _statusController.hide();
    _candidateWindow.hide();

    bool physical_caps_lock = _is_caps_lock_on(false);
    if (physical_caps_lock != _caps_lock) {
        _sync_caps_lock_state(physical_caps_lock);
    } else if ((GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0) {
        _sync_caps_lock_state(_is_caps_lock_on(false));
    }

    bool left_shift_down = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    bool right_shift_down = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
    bool generic_shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool shift_down = left_shift_down || right_shift_down || generic_shift_down;
    if (shift_down != _pollShiftDown) {
        WPARAM key = shift_down
            ? (right_shift_down ? VK_RSHIFT : (left_shift_down ? VK_LSHIFT : VK_SHIFT))
            : _pollShiftKey;
        _sync_status_key_edge(key, shift_down);
        _pollShiftDown = shift_down;
        if (shift_down)
            _pollShiftKey = key;
        else
            _pollShiftKey = VK_SHIFT;
    }
}

void TextService::_show_status_window_if_allowed() {
    if (_activated &&
        _inputFocused &&
        _config.status_window.enable &&
        _statusController.is_initialized()) {
        _statusController.show();
    }
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
    _seenKeyAfterActivate = false;

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
        if (_ensure_ipc_session() && _client.select_candidate(_sessionId, index, resp)) {
            if (resp.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
                _recreate_ipc_session_preserving_status();
                _candidateWindow.set_preedit("");
                _candidateWindow.hide();
                _composing = false;
                return;
            }
            if (resp.commit_text[0] != '\0') {
                std::wstring commit_text;
                int len = MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, nullptr, 0);
                if (len > 0) {
                    commit_text.resize(len - 1);
                    MultiByteToWideChar(CP_UTF8, 0, resp.commit_text, -1, &commit_text[0], len);
                }
                if (!commit_text.empty()) {
                    insert_text(commit_text);
                    // Need ITfContext to end composition; get it from thread manager.
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

    // Connect to server and query initial status before adding language bar buttons.
    // Pre-set the mode button to match the server before AddItem, so TSF reads the
    // correct icon on the first GetIcon call.
    bool initial_foreground_allows_input = _foreground_allows_input();
    cxxime::ImeStatus initial_status = {};
    initial_status.chinese_mode = true; // fallback default matching CLangBarItemButton ctor
    bool has_last_status = false;
    {
        std::lock_guard<std::mutex> lock(g_last_status_mutex);
        has_last_status = g_has_last_status;
        if (has_last_status)
            initial_status = g_last_status;
    }
    bool initial_caps_lock = initial_foreground_allows_input && _is_caps_lock_on(!has_last_status);
    _sessionId = 0;
    if (_ensure_ipc_session()) {
        if (initial_foreground_allows_input) {
            _sync_caps_lock_state(initial_caps_lock, &initial_status);
        }
        cxxime::IPCResponse status_resp = {};
        if (_ensure_ipc_session() &&
            _client.get_status(_sessionId, status_resp) && status_resp.status == cxxime::IPCStatus::OK) {
            initial_status = status_resp.ime_status;
        }
    }
    if (initial_foreground_allows_input && initial_caps_lock) {
        initial_status.caps_lock = true;
        auto caps_it = _config.ascii_switch_key.find("Caps_Lock");
        if (caps_it != _config.ascii_switch_key.end() && caps_it->second != "noop") {
            initial_status.chinese_mode = false;
        }
    }

    // Pre-set TextService state so _sync_ime_status sees no delta
    _chinese_mode = initial_status.chinese_mode;
    _caps_lock = initial_status.caps_lock;
    {
        std::lock_guard<std::mutex> lock(g_last_status_mutex);
        g_last_status = initial_status;
        g_has_last_status = true;
    }

    // Register language bar buttons
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        _modeButton = new CLangBarItemButton(tid, GUID_LBI_INPUTMODE);

        // Pre-set button state before AddItem to avoid flash
        _modeButton->update_from_status(initial_status);

        if (FAILED(pLangBarItemMgr->AddItem(_modeButton))) {
            CXXIME_LOG(L"Failed to add mode button to language bar");
        }

        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Mode language bar button registered");
    } else {
        CXXIME_LOG(L"Failed to get ITfLangBarItemMgr interface");
    }

    // Initialize status window controller
    if (initial_foreground_allows_input && _config.status_window.enable) {
        if (!_statusController.initialize(nullptr, &_client, _sessionId, &_config)) {
            CXXIME_LOG(L"StatusController: window creation failed, disabled");
        } else {
            _statusController.update_config(_config);
            _statusController.sync_status(initial_status);
        }
        // Set language bar callback for showing or hiding the status window.
        if (_modeButton) {
            _modeButton->set_show_status_callback([this]() {
                _config.status_window.enable = !_config.status_window.enable;
                _statusController.update_config(_config);
                _config.save(cxxime::user_data_path("default.json"));
                if (_config.status_window.enable) {
                    _show_status_window_if_allowed();
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
                if (_ensure_ipc_session())
                    _client.toggle_chinese(_sessionId, resp);
                CXXIME_LOG(L"menu_callback: toggle_chinese result status=%d, chinese=%d",
                           (int)resp.status, resp.ime_status.chinese_mode);
                if (resp.status == cxxime::IPCStatus::OK) {
                    _sync_ime_status(resp.ime_status);
                }
            });

            // Set toggle input mode callback.
            _modeButton->set_toggle_input_mode_callback([this]() {
                CXXIME_LOG(L"toggle_input_mode_callback: sessionId=%u", _sessionId);
                cxxime::IPCResponse resp = {};
                if (_ensure_ipc_session())
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
                show_about_dialog();
            });

            // Set explicit input mode callback.
            _modeButton->set_switch_input_mode_callback([this](int mode) {
                CXXIME_LOG(L"switch_input_mode_callback: mode=%d, sessionId=%u", mode, _sessionId);
                cxxime::IPCResponse resp = {};
                if (_ensure_ipc_session())
                    _client.switch_input_mode(_sessionId, static_cast<cxxime::InputMode>(mode), resp);
                if (resp.status == cxxime::IPCStatus::OK) {
                    _sync_ime_status(resp.ime_status);
                }
            });

            // Set quick phrase callback.
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

    // Avoid a redundant get_status IPC here. initial_status was already read
    // before language bar registration, so a second sync can block the UI
    // thread and trigger an extra icon refresh.
    // cxxime::IPCResponse resp = {};
    // if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
    //     _sync_ime_status(resp.ime_status);
    // }
    _activated = true;
    _start_state_poll_timer();
    if (!initial_foreground_allows_input)
        _sync_caps_lock_state(_is_caps_lock_on(false));
    if (_config.status_window.enable && _config.status_window.show_on_startup) {
        _update_input_focus_from_thread_mgr();
        if (_inputFocused) {
            _show_status_window_if_allowed();
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        }
    }
    return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
    CXXIME_LOG(L"Deactivate: sessionId=%u", _sessionId);
    _activated = false;
    _inputFocused = false;
    _seenKeyAfterActivate = false;
    _stop_state_poll_timer();

    // Hide status window immediately, then destroy it to avoid clicks during IPC teardown.
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
    _lastIpcHeartbeat = {};
    _ipcHealthy = true;
    // Keep the pipe connection for reuse. The next ActivateEx will create a
    // fresh server session and reconnect if the pipe has been closed.

    release_config_monitor_ref();

    _candidateWindow.destroy();

    // Unregister language bar button. The IME branding icon is provided by the TSF profile
    // registration, so CxxIME only owns this GUID_LBI_INPUTMODE status button.
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;
    if (_threadMgr && SUCCEEDED(_threadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLangBarItemMgr))) {
        if (_modeButton) {
            pLangBarItemMgr->RemoveItem(_modeButton);
            _modeButton->Release();
            _modeButton = nullptr;
        }
        pLangBarItemMgr->Release();
        CXXIME_LOG(L"Mode language bar button unregistered");
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
        _update_input_focus_from_thread_mgr();
        if (_inputFocused) {
            _show_status_window_if_allowed();
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        } else {
            if (_sessionId && _client.is_connected())
                _client.focus_out(_sessionId);
            _AbortComposition();
        }
    } else {
        _inputFocused = false;
        _start_state_poll_timer();
        // Switching away from CxxIME: hide status window immediately.
        // OnKillThreadFocus may not fire when switching IMEs within the same thread.
        if (_statusController.is_initialized())
            _statusController.hide();
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        _AbortComposition();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyDownPending = true;
    bool status_key =
        wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_SHIFT ||
        wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == VK_CONTROL ||
        wParam == VK_LMENU || wParam == VK_RMENU ||
        wParam == VK_LWIN || wParam == VK_RWIN ||
        wParam == VK_CAPITAL;
    if (!_context_allows_input(pic) && !status_key) {
        _inputFocused = false;
        _start_state_poll_timer();
        if (_statusController.is_initialized())
            _statusController.hide();
        _candidateWindow.hide();
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);

    // Modifier keys (Shift/Ctrl/Alt) must be eaten so TSF calls OnKeyDown,
    // which sends the key event to the server via IPC. Without this, TSF
    // passes the key directly to the app and OnKeyDown is never called.
    if (wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_SHIFT ||
        wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == VK_CONTROL ||
        wParam == VK_LMENU || wParam == VK_RMENU ||
        wParam == VK_LWIN || wParam == VK_RWIN ||
        wParam == VK_CAPITAL) {
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
        // Re-read *pfEaten; _ProcessKeyEvent set it during OnTestKeyDown.
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

    bool status_key =
        wParam == VK_LSHIFT || wParam == VK_RSHIFT || wParam == VK_SHIFT ||
        wParam == VK_LCONTROL || wParam == VK_RCONTROL || wParam == VK_CONTROL ||
        wParam == VK_LMENU || wParam == VK_RMENU ||
        wParam == VK_LWIN || wParam == VK_RWIN ||
        wParam == VK_CAPITAL;
    bool input_allowed = _context_allows_input(pic);
    if (!input_allowed && !status_key) {
        _inputFocused = false;
        _start_state_poll_timer();
        if (_statusController.is_initialized())
            _statusController.hide();
        _candidateWindow.hide();
        _AbortComposition();
        return false;
    }

    _inputFocused = input_allowed;
    if (_inputFocused) {
        _start_state_poll_timer();
    } else {
        _start_state_poll_timer();
        if (_statusController.is_initialized())
            _statusController.hide();
        _candidateWindow.hide();
        _AbortComposition();
    }
    _seenKeyAfterActivate = true;
    uint32_t modifiers = _get_modifiers();
    if (wParam == VK_CAPITAL) {
        bool target_caps_lock = !_caps_lock;
        if (target_caps_lock)
            modifiers |= 0x08;
        else
            modifiers &= ~0x08;
    }
    bool physical_caps_lock = (modifiers & 0x08) != 0;
    if (wParam != VK_CAPITAL && physical_caps_lock != _caps_lock)
        _sync_caps_lock_state(physical_caps_lock);

    // Config is reloaded by watcher thread (not keypress-driven).
    // Copy to local _config for consistent use during this keypress.
    {
        auto new_config = get_config();
        if (new_config.theme != _config.theme) {
            _candidateWindow.set_theme(cxxime::build_theme_from_config(new_config));
        }
        if (new_config.layout != _config.layout)
            _candidateWindow.set_layout(new_config.layout);
        if (new_config.font_size != _config.font_size ||
            new_config.theme != _config.theme)
            _statusController.update_config(new_config);
        _config = std::move(new_config);
    }

    // Record key event start time
    _key_event_start = std::chrono::steady_clock::now();

    CXXIME_LOG(L"_ProcessKeyEvent: vk=%u, mods=%u, composing=%d", (unsigned int)wParam, modifiers, _composing);

    cxxime::IPCResponse response = {};
    auto ipc_start = std::chrono::steady_clock::now();
    bool ok = _ensure_ipc_session() &&
              _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
        }
    }
    auto ipc_end = std::chrono::steady_clock::now();
    _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();

    // If IPC failed, reconnect and create a fresh server session.
    if (!ok) {
        _client.disconnect();
        _sessionId = 0;
        _ipcHealthy = false;
        if (_recreate_ipc_session_preserving_status()) {
            CXXIME_LOG(L"Reconnected, new sessionId=%u", _sessionId);
            ipc_start = std::chrono::steady_clock::now();
            response = {};
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response);
            ipc_end = std::chrono::steady_clock::now();
            _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();
        }
    }
    _ipcHealthy = ok;

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
        {
            cxxime::DiagnosticsConfig diag = cxxime::diagnostics_config();
            trace.slow = (trace.ipc_us >= diag.slow_ipc_us) ||
                         (trace.total_us >= diag.slow_total_us);
        }
        _enqueue_trace(trace);
        return false;
    }

    CXXIME_LOG(L"_ProcessKeyEvent: ok, vk=%u, ascii=%d, commit='%S', preedit='%S', composing=%d",
               (unsigned int)wParam, response.ascii_mode, response.commit_text, response.preedit, response.composing);

    // Sync mode state from engine only when server filled valid ime_status.
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
                _candidateWindow.set_page_info((int)response.page_current, (int)response.page_total);
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
        bool was_composing = _composing;
        _candidateWindow.hide();
        _candidateWindow.set_preedit("");
        if (_composing && _composition) update_composition(pic, L"");
        _end_composition(pic);
        _composing = false;
        // Only eat the key if there was an active composition to clean up.
        // Without this guard, keys like Backspace get eaten when not composing.
        if (was_composing)
            *pfEaten = TRUE;
        trace.result = TsfResult::CLEARED;
    } else {
        trace.result = TsfResult::REJECTED;
    }

    // Finalize and enqueue trace (async, non-blocking)
    auto total_end = std::chrono::steady_clock::now();
    trace.total_us = std::chrono::duration_cast<std::chrono::microseconds>(total_end - _key_event_start).count();
    {
        cxxime::DiagnosticsConfig diag = cxxime::diagnostics_config();
        trace.slow = (trace.ipc_us >= diag.slow_ipc_us) ||
                     (trace.window_us >= diag.slow_window_us) ||
                     (trace.total_us >= diag.slow_total_us);
    }
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
    bool ok = _ensure_ipc_session() &&
              _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response, true);
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response, true);
        }
    }

    // If IPC failed, reconnect and create a fresh server session.
    if (!ok) {
        CXXIME_LOG(L"_ProcessKeyUp: IPC failed, attempting reconnect");
        _client.disconnect();
        _sessionId = 0;
        _ipcHealthy = false;
        if (_recreate_ipc_session_preserving_status()) {
            CXXIME_LOG(L"_ProcessKeyUp: Reconnected, new sessionId=%u", _sessionId);
            response = {};
            ok = _client.process_key(_sessionId, (uint32_t)wParam, modifiers, response, true);
        }
    }
    _ipcHealthy = ok;

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
        if (_ensure_ipc_session() &&
            _client.toggle_chinese(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
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
    _update_input_focus_from_thread_mgr();
    _show_status_window_if_allowed();
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    _inputFocused = false;
    _start_state_poll_timer();
    if (_statusController.is_initialized())
        _statusController.hide();
    if (_sessionId && _client.is_connected())
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
    _inputFocused = _document_allows_input(pDocMgrFocus);
    if (!_inputFocused) {
        _start_state_poll_timer();
        if (_statusController.is_initialized())
            _statusController.hide();
        return S_OK;
    }

    _start_state_poll_timer();
    _show_status_window_if_allowed();

    // Sync status on focus change (user may have toggled via language bar)
    cxxime::IPCResponse resp = {};
    if (_ensure_ipc_session() &&
        _client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(resp.ime_status);
    }

    // Document focus changed; hide candidate window if switching away.
    if (_composing) {
        _candidateWindow.hide();
        _candidateWindow.set_preedit("");
        if (_sessionId && _client.is_connected())
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
