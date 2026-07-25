// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "text_service.h"
#include "globals.h"
#include "edit_session.h"
#include "candidate_ui_element.h"
#include "reading_ui_element.h"
#include "tsf_stage.h"
#include "display_attribute.h"
#include <cxxime/logging.h>
#include <cxxime/data_path.h>
#include <cxxime/diagnostics_config.h>
#include <cxxime/render_context.h>
#include <cxxime/stage_trace.h>
#include "preedit_mode.h"
#include "language_bar.h"
#include "about_dialog.h"
#include <algorithm>
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

static bool is_valid_caret_rect(const RECT& rc) {
    return (rc.left != 0 || rc.top != 0) &&
           rc.right >= rc.left && rc.bottom >= rc.top;
}

static void normalize_caret_rect_size(RECT* rc) {
    if (!rc)
        return;

    if (rc->right < rc->left)
        std::swap(rc->left, rc->right);
    if (rc->bottom < rc->top)
        std::swap(rc->top, rc->bottom);
    if (rc->right == rc->left)
        rc->right = rc->left + 1;
    if (rc->bottom == rc->top)
        rc->bottom = rc->top + 20;
}

static bool same_root_window(HWND a, HWND b) {
    if (!a || !b)
        return false;
    if (a == b || IsChild(a, b) || IsChild(b, a))
        return true;

    HWND root_a = GetAncestor(a, GA_ROOT);
    HWND root_b = GetAncestor(b, GA_ROOT);
    return root_a && root_a == root_b;
}

static bool is_top_level_window(HWND hwnd) {
    HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
    return root && root == hwnd;
}

static bool same_caret_position(const RECT& a, const RECT& b) {
    if (!is_valid_caret_rect(a) || !is_valid_caret_rect(b))
        return false;

    constexpr LONG kTolerancePx = 2;
    LONG dx = a.left - b.left;
    LONG dy = a.top - b.top;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return dx <= kTolerancePx && dy <= kTolerancePx;
}

static bool foreground_is_fullscreen() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || IsIconic(foreground) || !IsWindowVisible(foreground))
        return false;

    RECT client_rect = {};
    if (!GetClientRect(foreground, &client_rect) ||
        client_rect.right <= client_rect.left ||
        client_rect.bottom <= client_rect.top) {
        return false;
    }

    POINT client_top_left = { client_rect.left, client_rect.top };
    POINT client_bottom_right = { client_rect.right, client_rect.bottom };
    if (!ClientToScreen(foreground, &client_top_left) ||
        !ClientToScreen(foreground, &client_bottom_right)) {
        return false;
    }

    RECT screen_client_rect = {
        client_top_left.x,
        client_top_left.y,
        client_bottom_right.x,
        client_bottom_right.y,
    };

    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
    if (!monitor)
        return false;

    MONITORINFO monitor_info = { sizeof(monitor_info) };
    if (!GetMonitorInfoW(monitor, &monitor_info))
        return false;

    constexpr LONG tolerance = 2;
    const RECT& screen = monitor_info.rcMonitor;
    return screen_client_rect.left <= screen.left + tolerance &&
           screen_client_rect.top <= screen.top + tolerance &&
           screen_client_rect.right >= screen.right - tolerance &&
           screen_client_rect.bottom >= screen.bottom - tolerance;
}

static std::mutex g_last_status_mutex;
static bool g_has_last_status = false;
static cxxime::ImeStatus g_last_status;

static constexpr UINT kStatePollIntervalMs = 30;
static constexpr int kCandidatePendingFallbackDelayMs = 30;
static std::mutex g_state_poll_timer_mutex;
static std::unordered_map<UINT_PTR, TextService*> g_state_poll_timers;

static bool is_shift_key(WPARAM key) {
    return key == VK_LSHIFT || key == VK_RSHIFT || key == VK_SHIFT;
}

static bool is_status_key(WPARAM key) {
    return is_shift_key(key) ||
           key == VK_LCONTROL || key == VK_RCONTROL || key == VK_CONTROL ||
           key == VK_LMENU || key == VK_RMENU ||
           key == VK_LWIN || key == VK_RWIN ||
           key == VK_CAPITAL;
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

static const char* bool_json(bool value) {
    return value ? "true" : "false";
}

static std::wstring utf8_to_wstring(const char* text) {
    if (!text || text[0] == '\0')
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &result[0], len);
    return result;
}

static bool tsf_should_log_event(bool important) {
    cxxime::DiagnosticsConfig config = cxxime::diagnostics_config();
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kOff)
        return false;
    if (config.trace_mode == cxxime::DiagnosticTraceMode::kError)
        return important;
    return true;
}

static void foreground_class_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return;

    wchar_t class_name[64] = {};
    if (!GetClassNameW(foreground, class_name, ARRAYSIZE(class_name)))
        return;

    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
}

static void current_process_utf8(char* out, int out_size) {
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)))
        return;

    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    WideCharToMultiByte(CP_UTF8, 0, base, -1, out, out_size, nullptr, nullptr);
    out[out_size - 1] = '\0';
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

void TextService::_enqueue_event_trace(const char* event, const char* detail, bool important) {
    if (!tsf_should_log_event(important))
        return;

    char foreground_class[96] = {};
    foreground_class_utf8(foreground_class, sizeof(foreground_class));
    char process_name[MAX_PATH] = {};
    current_process_utf8(process_name, sizeof(process_name));

    TsfTraceEntry entry;
    entry.len = snprintf(entry.json, sizeof(entry.json),
                         "{\"event\":\"%s\",\"detail\":\"%s\",\"session\":%u,"
                         "\"focused\":%s,\"chinese\":%s,\"caps\":%s,"
                         "\"proc\":\"%s\",\"fg\":\"%s\"}",
                         event ? event : "", detail ? detail : "", _sessionId,
                         bool_json(_inputFocused), bool_json(_chinese_mode),
                         bool_json(_caps_lock), process_name, foreground_class);
    if (entry.len <= 0 || entry.len >= static_cast<int>(sizeof(entry.json)))
        return;

    tsf_ensure_writer_started();
    tsf_queue_try_push(entry);
}

// TextService lifecycle

TextService::TextService() {}

TextService::~TextService() {
    _stop_state_poll_timer();
    set_composition_context(nullptr);
    _stop_host_takeover_runtime();
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
    else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
        *ppvObj = static_cast<ITfTextLayoutSink*>(this);
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

void TextService::update_candidate_position(const RECT& rc,
                                            ITfContext* context,
                                            bool from_layout_change) {
    RECT final_rect = rc;
    bool resolved = is_valid_caret_rect(final_rect);
    bool used_trusted_native = false;
    if (!resolved) {
        trace_caret_event("move", "invalid", false, &rc, E_INVALIDARG, true);
        if (context && _resolve_context_native_caret_rect(context, &final_rect)) {
            used_trusted_native = true;
        } else if (!_resolve_native_caret_rect(&final_rect)) {
            return;
        }
        resolved = true;
        trace_caret_event("move", "native_fallback", true, &final_rect, S_FALSE, true);
    } else {
        RECT native_rect = {};
        if (context && _resolve_context_native_caret_rect(context, &native_rect)) {
            final_rect = native_rect;
            used_trusted_native = true;
        }
    }

    _caretRect = final_rect;
    const bool ui_element_only = (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    const bool original_ui_allowed =
        _candidateUiElement && _candidateUiElement->wants_external_window();
    if (ui_element_only && !original_ui_allowed) {
        trace_caret_event("move", "ui_element_only", false, &final_rect);
        return;
    }
    if (!_candidateWindow.is_visible()) {
        if (_candidateShowPending) {
            if (!from_layout_change && !used_trusted_native &&
                _candidatePendingHasStaleRect &&
                same_caret_position(final_rect, _candidatePendingStaleRect)) {
                auto now = std::chrono::steady_clock::now();
                if (_candidateShowPendingSince.time_since_epoch().count() != 0 &&
                    now - _candidateShowPendingSince <
                        std::chrono::milliseconds(kCandidatePendingFallbackDelayMs)) {
                    return;
                }
            }
            _candidateShowPending = false;
            _candidatePendingHasStaleRect = false;
            _candidatePendingStaleRect = {};
            _candidateShowPendingSince = {};
            _candidateWindow.move_to_caret(final_rect);
            trace_caret_event("move", "candidate_window", resolved, &final_rect);
            _show_candidate_window("show:preedit");
            return;
        }
        trace_caret_event("move", "hidden", false, &final_rect);
        return;
    }

    trace_caret_event("move", "candidate_window", resolved, &final_rect);
    _candidateWindow.move_to_caret(final_rect);
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

    bool shell_surface = 
        wcscmp(class_name, L"CabinetWClass") == 0 ||
        wcscmp(class_name, L"ExploreWClass") == 0 ||
        wcscmp(class_name, L"ShellTabWindowClass") == 0 ||
        wcscmp(class_name, L"#32770") == 0;
    if (!shell_surface)
        return true;

    DWORD foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO gti = { sizeof(gti) };
    if (!foreground_thread || !GetGUIThreadInfo(foreground_thread, &gti))
        return true;

    auto belongs_to_foreground = [foreground](HWND hwnd) {
        return hwnd && (hwnd == foreground || IsChild(foreground, hwnd));
    };

    if (belongs_to_foreground(gti.hwndCaret))
        return true;
    
    if (!belongs_to_foreground(gti.hwndFocus))
        return true;

    wchar_t focus_class[64] = {};
    GetClassNameW(gti.hwndFocus, focus_class, ARRAYSIZE(focus_class));
    if (wcscmp(focus_class, L"Edit") == 0 ||
        wcsncmp(focus_class, L"RichEdit", 8) == 0 ||
        wcscmp(focus_class, L"RICHEDIT50W") == 0) {
        return true;
    }

    if (wcscmp(focus_class, L"SysListView32") == 0 ||
        wcscmp(focus_class, L"SysTreeView32") == 0 ||
        wcscmp(focus_class, L"DirectUIHWND") == 0 ||
        wcscmp(focus_class, L"DUIViewWndClassName") == 0 ||
        wcscmp(focus_class, L"SHELLDLL_DefView") == 0) {
        return false;
    }

	return true;
}

bool TextService::_context_belongs_to_foreground(ITfContext* context) const {
    if (!context)
        return false;

    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view)
        return true;

    HWND context_hwnd = nullptr;
    HRESULT hr = view->GetWnd(&context_hwnd);
    view->Release();

    if (FAILED(hr) || !context_hwnd)
        return true;

    if (context_hwnd == foreground || IsChild(foreground, context_hwnd))
        return true;

    HWND context_root = GetAncestor(context_hwnd, GA_ROOT);
    HWND foreground_root = GetAncestor(foreground, GA_ROOT);
    return context_root && context_root == foreground_root;
}

bool TextService::_advise_text_layout_sink(ITfDocumentMgr* doc_mgr) {
    _unadvise_text_layout_sink();
    if (!doc_mgr)
        return true;

    ITfContext* context = nullptr;
    if (FAILED(doc_mgr->GetTop(&context)) || !context)
        return false;

    ITfSource* source = nullptr;
    if (FAILED(context->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))) ||
        !source) {
        context->Release();
        return false;
    }

    DWORD cookie = TF_INVALID_COOKIE;
    HRESULT hr = source->AdviseSink(IID_ITfTextLayoutSink,
                                    static_cast<ITfTextLayoutSink*>(this),
                                    &cookie);
    source->Release();
    if (FAILED(hr)) {
        context->Release();
        return false;
    }

    _textLayoutSinkContext = context;
    _dwTextLayoutSinkCookie = cookie;
    return true;
}

void TextService::_unadvise_text_layout_sink() {
    if (_textLayoutSinkContext) {
        ITfSource* source = nullptr;
        if (_dwTextLayoutSinkCookie != TF_INVALID_COOKIE &&
            SUCCEEDED(_textLayoutSinkContext->QueryInterface(IID_ITfSource,
                                                             reinterpret_cast<void**>(&source))) &&
                                                             source) {
            source->UnadviseSink(_dwTextLayoutSinkCookie);
            source->Release();
        }
        _textLayoutSinkContext->Release();
        _textLayoutSinkContext = nullptr;
    }
    _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
}

void TextService::_request_candidate_position_update(ITfContext* pic, 
                                                     const char* reason,
                                                     bool from_layout_change) {
    if (!pic || !_composing || (!_candidateWindow.is_visible() && !_candidateShowPending))
        return;
    const bool ui_element_only = (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;
    const bool original_ui_allowed =
        _candidateUiElement && _candidateUiElement->wants_external_window();
    if (ui_element_only && !original_ui_allowed) {
        trace_caret_event("request_update", "ui_element_only", false, nullptr);
        return;
    }

    EditSession* session = new (std::nothrow) EditSession(this, pic);
    if (!session) {
        trace_caret_event("request_update", "alloc_failed", false, nullptr, E_OUTOFMEMORY, true);
        return;
    }

    session->set_action(EditSession::Action::UPDATE_CANDIDATE_POSITION);
    session->set_position_update_from_layout_change(from_layout_change);
    HRESULT hr = E_FAIL;
    HRESULT request_hr =
        pic->RequestEditSession(_clientId, session, TF_ES_READ | TF_ES_ASYNCDONTCARE, &hr);
    trace_caret_event("request_update", reason ? reason : "async",
                      SUCCEEDED(request_hr) && SUCCEEDED(hr), nullptr,
                      FAILED(request_hr) ? request_hr : hr,
                      FAILED(request_hr) || FAILED(hr));
    session->Release();
}

bool TextService::_read_context_compartment_bool(ITfContext* context, REFGUID guid,
                                                 bool* value) const {
    if (!context || !value)
        return false;

    ITfCompartmentMgr* compartment_mgr = nullptr;
    if (FAILED(context->QueryInterface(IID_ITfCompartmentMgr,
                                       reinterpret_cast<void**>(&compartment_mgr))) ||
        !compartment_mgr) {
        return false;
    }

    ITfCompartment* compartment = nullptr;
    HRESULT hr = compartment_mgr->GetCompartment(guid, &compartment);
    compartment_mgr->Release();
    if (FAILED(hr) || !compartment)
        return false;
    
    VARIANT current = {};
    VariantInit(&current);
    bool found = false;
    if (SUCCEEDED(compartment->GetValue(&current))) {
        if (current.vt == VT_I4 || current.vt == VT_INT) {
            *value = current.lVal != 0;
            found = true;
        } else if (current.vt == VT_UI4 || current.vt == VT_UINT) {
            *value = current.ulVal != 0;
            found = true;
        } else if (current.vt == VT_BOOL) {
            *value = current.boolVal != VARIANT_FALSE;
            found = true;
        }
    }
    VariantClear(&current);
    compartment->Release();
    return found;
}

bool TextService::_context_keyboard_disabled(ITfContext* context) const {
    if (!context)
        return true;

    bool disabled = false;
    if (_read_context_compartment_bool(context, GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled) &&
        disabled) {
        return true;
    }

    bool empty_context = false;
    if (_read_context_compartment_bool(context, GUID_COMPARTMENT_EMPTYCONTEXT, &empty_context) &&
        empty_context) {
        return true;
    }

    return false;
}

const char* TextService::_input_context_block_reason(ITfContext* context) const {
    if (!context)
        return "no_context";
    if (!_foreground_allows_input())
        return "foreground_denied";
    if (!_context_belongs_to_foreground(context))
        return "context_not_foreground";
    if (_context_keyboard_disabled(context))
        return "keyboard_disabled";

    TF_STATUS status = {};
    if (FAILED(context->GetStatus(&status)))
        return nullptr;

    if ((status.dwDynamicFlags & TF_SD_READONLY) != 0)
        return "readonly";

    return nullptr;
}

bool TextService::_context_allows_input(ITfContext* context) const {
    return _input_context_block_reason(context) == nullptr;
}

bool TextService::_document_allows_input(ITfDocumentMgr* doc_mgr) const {
	if (!doc_mgr)
		return false;

	ITfContext* context = nullptr;
	HRESULT hr = doc_mgr->GetTop(&context);
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
	if (!focused)
		_hide_status_window("hide:focus_query_unfocused");
	return focused;
}

bool TextService::_ensure_ipc_session() {
    if (_sessionId && _client.is_connected())
        return true;

    if (!_client.is_connected() &&
        !_client.connect(cxxime::IPC_PIPE_BASE_NAME, kTsfIpcTimeoutMs)) {
        if (_ipcHealthy) {
            CXXIME_LOG(L"IPC unavailable");
            _enqueue_event_trace("ipc_session", "connect_failed", true);
        }
        _ipcHealthy = false;
        return false;
    }

    uint32_t session_id = 0;
    if (!_client.start_session(session_id) || session_id == 0) {
        CXXIME_LOG(L"Failed to start IPC session");
        if (_ipcHealthy)
            _enqueue_event_trace("ipc_session", "start_failed", true);
        _sessionId = 0;
        _ipcHealthy = false;
        _client.disconnect();
        return false;
    }

    _sessionId = session_id;
    _ipcHealthy = true;
    _lastIpcHeartbeat = std::chrono::steady_clock::now();
    CXXIME_LOG(L"IPC session ready, sessionId=%u", _sessionId);
    _enqueue_event_trace("ipc_session", "ready");
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
    _enqueue_event_trace("ipc_session", "recreated", true);
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
        if (_ipcHealthy)
            _enqueue_event_trace("ipc_session", "heartbeat_reconnect_failed", true);
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
        _enqueue_event_trace("ipc_session", "heartbeat_invalid_session", true);
        return _recreate_ipc_session_preserving_status();
    }

    CXXIME_LOG(L"IPC heartbeat failed, reconnecting");
    if (_ipcHealthy)
        _enqueue_event_trace("ipc_session", "heartbeat_failed", true);
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
            _show_status_window_if_allowed("show:poll_focus_in");
            if (_sessionId && _client.ensure_connected())
                _client.focus_in(_sessionId);
        }
        if (_candidateShowPending && _composing) {
            ITfContext* context = _current_edit_context_for_composition();
            if (context) {
                _request_candidate_position_update(context, "show:pending_timeout");
                context->Release();
            }
        }
        return;
    }

    if (_inputFocused) {
        _inputFocused = false;
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        _AbortComposition();
    }
    _hide_status_window("hide:poll_unfocused");
    _hide_candidate_window("hide:poll_unfocused");
    _end_reading_ui_element("hide:poll_unfocused_reading");

    bool physical_caps_lock = _is_caps_lock_on(false);
    if (physical_caps_lock != _caps_lock) {
        _sync_caps_lock_state(physical_caps_lock);
    } else if ((GetAsyncKeyState(VK_CAPITAL) & 0x0001) != 0) {
        _sync_caps_lock_state(_is_caps_lock_on(false));
    }
}

void TextService::_show_status_window_if_allowed(const char* reason) {
    if (_activated &&
        _inputFocused &&
        _config.status_window.enable &&
        _statusController.is_initialized()) {
        if (foreground_is_fullscreen()) {
            _hide_status_window("hide:fullscreen_foreground");
            return;
        }
        if (!_statusController.is_visible())
            _enqueue_event_trace("status_window", reason);
        _statusController.show();
    }
}

void TextService::_hide_status_window(const char* reason) {
    if (!_statusController.is_initialized())
        return;
    if (_statusController.is_visible())
        _enqueue_event_trace("status_window", reason);
    _statusController.hide();
}

void TextService::_show_candidate_window(const char* reason) {
    if (foreground_is_fullscreen()) {
        _hide_external_candidate_window("hide:fullscreen_foreground");
        return;
    }
    if (!_candidateWindow.is_visible())
        _enqueue_event_trace("candidate_window", reason);
    _candidateWindow.show();
}

void TextService::_hide_external_candidate_window(const char* reason) {
    _candidateShowPending = false;
    _candidatePendingHasStaleRect = false;
    _candidatePendingStaleRect = {};
    _candidateShowPendingSince = {};
    if (_candidateWindow.is_visible())
        _enqueue_event_trace("candidate_window", reason);
    _candidateWindow.hide();
}

void TextService::_hide_candidate_window(const char* reason) {
    _hide_external_candidate_window(reason);
    if (_candidateUiElement) {
        _candidateUiElement->end(_threadMgr);
    }
    _set_host_candidate_notifications_open(false);
}

void TextService::_update_reading_ui_element(ITfContext* context, const std::wstring& reading) {
    if (!_readingUiElement || !context || reading.empty()) {
        _end_reading_ui_element("hide:reading_empty");
        return;
    }

    bool was_active = _readingUiElement->is_active();
    _readingUiElement->set_reading(context, reading);
    bool external = _readingUiElement->begin(_threadMgr);
    _readingUiElement->notify_update(_threadMgr);

    if (!was_active) {
        char detail[64] = {};
        snprintf(detail, sizeof(detail), "reading external=%s len=%u",
                 external ? "true" : "false", static_cast<unsigned int>(reading.size()));
        _enqueue_event_trace("ui_element", detail);
    }
}

void TextService::_end_reading_ui_element(const char* reason) {
    if (!_readingUiElement)
        return;
    bool was_active = _readingUiElement->is_active();
    _readingUiElement->end(_threadMgr);
    if (was_active)
        _enqueue_event_trace("ui_element", reason ? reason : "hide:reading");
}

void TextService::_trace_input_decision(const char* block_reason) {
    if (!block_reason) {
        if (!_lastInputBlockReason.empty()) {
            _lastInputBlockReason.clear();
            _enqueue_event_trace("input_context", "allowed");
        }
        return;
    }

    if (_lastInputBlockReason == block_reason)
        return;
    _lastInputBlockReason = block_reason;
    _enqueue_event_trace("input_context", block_reason);
}

// ITfTextInputProcessorEx
STDMETHODIMP TextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
    return ActivateEx(ptim, tid, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) {
    OutputDebugStringA("[CxxIME] ActivateEx called\n");
    CXXIME_LOG(L"ActivateEx: clientId=%u, flags=%u", tid, dwFlags);
    {
        char detail[64] = {};
        snprintf(detail, sizeof(detail), "flags=0x%08x", static_cast<unsigned int>(dwFlags));
        _enqueue_event_trace("activate", detail, true);
    }

    _config = get_config();
    init_config_monitor();
    add_config_monitor_ref();

    _threadMgr = ptim;
    _threadMgr->AddRef();
    _clientId = tid;
    _activateFlags = dwFlags;
    cxxime_tsf::trace_stage_runtime_activate(dwFlags, tid);
    if ((_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
        _start_host_takeover_runtime();
    }
    _seenKeyAfterActivate = false;
    _register_display_attribute_atom();

    _register_key_event_sink();
    _register_preserved_key();

    _register_thread_sinks();

    // Create candidate window (use HWND_MESSAGE parent since TSF runs in-app)
    _candidateWindow.create(nullptr, _config);
    _candidateWindow.set_layout(_config.layout);
    _candidateWindow.set_click_callback([this](int index) {
        select_candidate_from_ui(static_cast<UINT>(index));
    });
    _candidateUiElement = new (std::nothrow) CandidateUIElement(this);
    _readingUiElement = new (std::nothrow) ReadingUIElement(this);

    // Connect to server and query initial status before adding language bar buttons.
    // Pre-set the mode button to match the server before AddItem, so TSF reads the
    // correct icon on the first GetIcon call.
    bool initial_input_allows_input = _query_input_focus_from_thread_mgr();
    cxxime::ImeStatus initial_status = {};
    initial_status.chinese_mode = true; // fallback default matching CLangBarItemButton ctor
    bool has_last_status = false;
    {
        std::lock_guard<std::mutex> lock(g_last_status_mutex);
        has_last_status = g_has_last_status;
        if (has_last_status)
            initial_status = g_last_status;
    }
    bool initial_caps_lock = initial_input_allows_input && _is_caps_lock_on(!has_last_status);
    _sessionId = 0;
    if (_ensure_ipc_session()) {
        if (initial_input_allows_input) {
            _sync_caps_lock_state(initial_caps_lock, &initial_status);
        }
        cxxime::IPCResponse status_resp = {};
        if (_ensure_ipc_session() &&
            _client.get_status(_sessionId, status_resp) && status_resp.status == cxxime::IPCStatus::OK) {
            initial_status = status_resp.ime_status;
        }
    }
    if (initial_input_allows_input && initial_caps_lock) {
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

    // Initialize status window controller hidden; visibility is gated by input focus.
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
                _show_status_window_if_allowed("show:language_bar_toggle");
            } else {
                _hide_status_window("hide:language_bar_toggle");
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

    // Avoid a redundant get_status IPC here. initial_status was already read
    // before language bar registration, so a second sync can block the UI
    // thread and trigger an extra icon refresh.
    // cxxime::IPCResponse resp = {};
    // if (_client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
    //     _sync_ime_status(resp.ime_status);
    // }
    _activated = true;
    _start_state_poll_timer();
    if (!initial_input_allows_input)
        _sync_caps_lock_state(_is_caps_lock_on(false));
    if (_config.status_window.enable && _config.status_window.show_on_startup) {
        _update_input_focus_from_thread_mgr();
        if (_inputFocused) {
            _show_status_window_if_allowed("show:activate_startup");
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
    _hide_status_window("hide:deactivate");
    _statusController.shutdown();

    if (_sessionId) {
        // Commit any pending composition before ending session
        if (_composing) {
            cxxime::IPCResponse resp = {};
            _client.commit_composition(_sessionId, resp);
            if (resp.commit_text[0] != '\0' && _threadMgr) {
                std::wstring commit_text = utf8_to_wstring(resp.commit_text);
                if (!commit_text.empty()) {
                    ITfContext* pContext = _current_edit_context_for_composition();
                    if (pContext) {
                        _commit_text(pContext, commit_text, true);
                        pContext->Release();
                    } else {
                        insert_text(commit_text, true);
                    }
                }
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

    _hide_candidate_window("hide:deactivate_candidates");
    _end_reading_ui_element("hide:deactivate_reading");
    _unadvise_text_layout_sink();
    set_composition_context(nullptr);
    if (_candidateUiElement) {
        _candidateUiElement->Release();
        _candidateUiElement = nullptr;
    }
    if (_readingUiElement) {
        _readingUiElement->Release();
        _readingUiElement = nullptr;
    }
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

    _unregister_thread_sinks();

    _unregister_key_event_sink();
    _unregister_preserved_key();

    if (_threadMgr) {
        _threadMgr->Release();
        _threadMgr = nullptr;
    }
    _clientId = TF_CLIENTID_NULL;
    if ((_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0) {
        _stop_host_takeover_runtime();
    }

    return S_OK;
}

// ITfKeyEventSink
STDMETHODIMP TextService::OnSetFocus(BOOL fForeground) {
    if (fForeground) {
        _update_input_focus_from_thread_mgr();
        if (_inputFocused) {
            _show_status_window_if_allowed("show:set_focus");
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
        _hide_status_window("hide:ime_focus_lost");
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        _AbortComposition();
    }
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyDownPending = true;
    bool status_key = is_status_key(wParam);
    const char* test_block_reason = _input_context_block_reason(pic);
    _trace_input_decision(test_block_reason);
    if (test_block_reason && !status_key) {
        _inputFocused = false;
        _start_state_poll_timer();
        _hide_status_window("hide:test_key_context_rejected");
        _hide_candidate_window("hide:test_key_context_rejected");
        _end_reading_ui_element("hide:test_key_context_rejected_reading");
        *pfEaten = FALSE;
        return S_OK;
    }

    *pfEaten = _ProcessKeyEvent(pic, wParam, lParam, pfEaten);

    // Status keys are already sent during OnTestKeyDown. Reporting them as
    // eaten keeps TSF on the paired OnKeyDown/OnKeyUp path, where the pending
    // flags prevent duplicate delivery.
    if (is_status_key(wParam)) {
        *pfEaten = TRUE;
    }

    OutputDebugStringA("[CxxIME] OnTestKeyDown\n");
    CXXIME_LOG(L"OnTestKeyDown: vk=%u, eaten=%d, sessionId=%u", (unsigned int)wParam, *pfEaten, _sessionId);
    return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    _fTestKeyUpPending = true;
    *pfEaten = (wParam == VK_CAPITAL) ? TRUE : FALSE;
    CXXIME_LOG(L"OnTestKeyUp: vk=%u, sessionId=%u", (unsigned int)wParam, _sessionId);
    if (wParam != VK_CAPITAL)
        _ProcessKeyUp(wParam, lParam);
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
        *pfEaten = (wParam == VK_CAPITAL) ? TRUE : FALSE;
        return S_OK;
    }
    if (wParam == VK_CAPITAL) {
        *pfEaten = TRUE;
        return S_OK;
    }
    // Some apps call OnKeyUp without OnTestKeyUp
    _ProcessKeyUp(wParam, lParam);
    *pfEaten = FALSE;
    return S_OK;
}

bool TextService::_ProcessKeyEvent(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    *pfEaten = FALSE;
    _stageInputId = cxxime::stage_trace_input_id(static_cast<uint32_t>(wParam), lParam);

    bool status_key = is_status_key(wParam);
    const char* block_reason = _input_context_block_reason(pic);
    bool input_allowed = block_reason == nullptr;
    _trace_input_decision(block_reason);
    if (!input_allowed && !status_key) {
        cxxime_tsf::trace_stage_key_route(
        _stageInputId, _stageCompositionId, static_cast<uint32_t>(wParam), 0, "blocked",
        block_reason ? block_reason : "input_context");
        _inputFocused = false;
        _start_state_poll_timer();
        _hide_status_window("hide:key_context_rejected");
        _hide_candidate_window("hide:key_context_rejected");
        _end_reading_ui_element("hide:key_context_rejected_reading");
        _AbortComposition();
        return false;
    }

    _inputFocused = input_allowed;
    if (_inputFocused) {
        _start_state_poll_timer();
    } else {
        _start_state_poll_timer();
        _hide_status_window("hide:key_context_status_only");
        _hide_candidate_window("hide:key_context_status_only");
        _end_reading_ui_element("hide:key_context_status_only_reading");
        _AbortComposition();
    }
    _seenKeyAfterActivate = true;
    uint32_t modifiers = _get_modifiers();
    if (wParam == VK_CAPITAL) {
        // Windows reports VK_CAPITAL after the lock bit has toggled. CxxIME's
        // engine expects the final CapsLock state, so do not infer it from the
        // cached IME state, which may lag when focus moved through non-input UI.
        bool target_caps_lock = _is_caps_lock_on(false);
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
    uint32_t engine_calls = 0;
    auto process_key = [&]() {
        ++engine_calls;
        return _client.process_key(_sessionId, static_cast<uint32_t>(wParam), modifiers, response);
    };
    auto ipc_start = std::chrono::steady_clock::now();
    bool ok = _ensure_ipc_session() && process_key();
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = process_key();
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
            ok = process_key();
            ipc_end = std::chrono::steady_clock::now();
            _last_ipc_us = std::chrono::duration_cast<std::chrono::microseconds>(ipc_end - ipc_start).count();
        }
    }
    _ipcHealthy = ok;
    cxxime_tsf::trace_stage_key_route(
        _stageInputId, _stageCompositionId, static_cast<uint32_t>(wParam), engine_calls,
        ok ? "processed" : "ipc_failed");

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
        _hide_candidate_window("hide:commit");
        _end_reading_ui_element("hide:commit_reading");
        _candidateWindow.set_preedit("");
        std::wstring commit_text = utf8_to_wstring(response.commit_text);
            if (!commit_text.empty()) {
                _commit_text(pic, commit_text, true);
                _composing = false;
                _lastInlineCompositionText.clear();
                *pfEaten = TRUE;
            }
        trace.result = TsfResult::COMMITTED;
        trace.candidate_count = response.candidate_count;
    } else if (response.preedit[0] != '\0') {
        ensure_stage_composition_id();
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

        const bool has_candidates = response.candidate_count > 0;
        cxxime::CandidatePage page;
        if (has_candidates) {
            page.highlighted = static_cast<int>(response.highlighted);
            for (uint32_t i = 0; i < response.candidate_count && i < 10; ++i) {
                cxxime::Candidate candidate;
                candidate.text = response.candidates[i];
                page.candidates.push_back(std::move(candidate));
            }
        }

        CXXIME_LOG(L"_ProcessKeyEvent: start_comp=%d, _composing=%d, _composition=%d, inline='%s'",
                   decision.start_composition, _composing, _composition != nullptr,
                   decision.inline_text.c_str());

        const bool ui_element_only =
            (_activateFlags & TF_TMF_UIELEMENTENABLEDONLY) != 0;

            cxxime_tsf::trace_stage_context(
                _stageInputId, _stageCompositionId, pic, _threadMgr,
                ui_element_only ? "candidate_first_standard_tsf_compat" : "standard_tsf");

        _caretRect = {};
        _lastInlineCompositionText = ui_element_only
            ? preedit
            : (decision.start_composition ? decision.inline_text : L"");
        bool external_candidate_window = true;
        bool candidate_ui_published = false;
        if (ui_element_only) {
            _end_reading_ui_element("hide:candidate_mirror_no_reading");
            external_candidate_window = _publish_candidate_ui_element(
                page, response.candidate_count, response.page_current, response.page_total);
            candidate_ui_published = true;
            update_composition(pic, preedit, true, true);
        } else if (decision.start_composition) {
            _update_reading_ui_element(pic, preedit);
            update_composition(pic, decision.inline_text, true, true);
        } else {
            _update_reading_ui_element(pic, preedit);
            if (_composing && _composition) {
                _end_composition(pic);
            }
            _composing = true;
            update_composition(pic, L"", true, true);
        }
        *pfEaten = TRUE;

        std::string popup_preedit =
            ui_element_only ? response.preedit :
            (decision.show_preedit_in_popup ? response.preedit : "");
        _candidateWindow.set_preedit(popup_preedit);

        const bool has_preedit = !popup_preedit.empty();

        CXXIME_LOG(L"_ProcessKeyEvent: has_cand=%d, has_preedit=%d, cand_count=%u",
                   has_candidates, has_preedit, response.candidate_count);

        auto window_start = std::chrono::steady_clock::now();

        if (has_candidates || has_preedit) {
            if (!candidate_ui_published) {
                external_candidate_window = _publish_candidate_ui_element(
                    page, response.candidate_count, response.page_current, response.page_total);
            }
            if (external_candidate_window) {
                bool candidate_was_visible = _candidateWindow.is_visible();
                _candidateWindow.set_page_info((int)response.page_current, (int)response.page_total);
                _candidateWindow.update(page);

                // Query the current caret before falling back to the cached rectangle. The cache may
                // still point to the previous composition after a commit/new preedit boundary.
                RECT caretRect = {};
                bool caretResolved = false;
                RECT trustedNativeRect = {};
                bool hasTrustedNativeCaret =
                    _resolve_context_native_caret_rect(pic, &trustedNativeRect);
                EditSession* pCaretSession = new (std::nothrow) EditSession(this, pic);
                if (pCaretSession) {
                    pCaretSession->set_action(EditSession::Action::QUERY_CARET);
                    HRESULT hr = E_FAIL;
                    HRESULT request_hr = pic->RequestEditSession(_clientId, pCaretSession,
                                                    TF_ES_READ | TF_ES_SYNC, &hr);
                    if (SUCCEEDED(request_hr) && SUCCEEDED(hr))
                        caretResolved = pCaretSession->get_caret_rect(caretRect);
                    trace_caret_event("show_query", "sync_edit", caretResolved,
                                      caretResolved ? &caretRect : nullptr,
                                      FAILED(request_hr) ? request_hr : hr,
                                      !caretResolved);
                    pCaretSession->Release();
                }
                if (!caretResolved) {
                    if (hasTrustedNativeCaret) {
                        caretRect = trustedNativeRect;
                        caretResolved = true;
                    } else {
                        caretRect = _resolve_caret_rect(pic);
                        trace_caret_event("show_query", "fallback",
                                          is_valid_caret_rect(caretRect), &caretRect, S_FALSE,
                                          true);
                        caretResolved = is_valid_caret_rect(caretRect);
                    }
                } else if (hasTrustedNativeCaret) {
                    caretRect = trustedNativeRect;
                }

                // HWND-backed editors expose a trustworthy native caret. TSF-only hosts can make
                // GetTextExt lag one layout cycle after a new composition, and the stale rectangle
                // is not always identical to the previous popup position. Defer first show in those
                // hosts; the async edit session or OnLayoutChange will show the popup once the
                // range rectangle catches up.
                bool defer_show = !candidate_was_visible && !hasTrustedNativeCaret;
                if (defer_show) {
                    bool was_pending = _candidateShowPending;
                    _candidateShowPending = true;
                    _candidatePendingStaleRect = caretRect;
                    _candidatePendingHasStaleRect = is_valid_caret_rect(caretRect);
                    if (!was_pending ||
                        _candidateShowPendingSince.time_since_epoch().count() == 0) {
                        _candidateShowPendingSince = std::chrono::steady_clock::now();
                    }
                    _request_candidate_position_update(pic, "show:preedit_layout_follow");
                } else {
                    _candidateShowPending = false;
                    _candidatePendingHasStaleRect = false;
                    _candidatePendingStaleRect = {};
                    _candidateShowPendingSince = {};
                    _candidateWindow.move_to_caret(caretRect);
                    trace_caret_event("show_move", "initial", true, &caretRect);
                    _show_candidate_window("show:preedit");
                    _request_candidate_position_update(pic, "show:preedit_layout_follow");
                }
            } else {
                _hide_external_candidate_window("hide:tsf_ui_integrated");
            }
        } else {
            _hide_candidate_window("hide:no_candidates");
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
        _hide_candidate_window("hide:clear");
        _end_reading_ui_element("hide:clear_reading");
        _candidateWindow.set_preedit("");
        if (_composing && _composition) {
            update_composition(pic, L"");
        }
        _end_composition(pic);
        _composing = false;
        // Only eat the key if there was an active composition to clean up.
        // Without this guard, keys like Backspace get eaten when not composing.
        if (was_composing)
            *pfEaten = TRUE;
        _lastInlineCompositionText.clear();
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

    cxxime_tsf::trace_stage_key_result(
        _stageInputId, _stageCompositionId, static_cast<uint32_t>(wParam), *pfEaten != FALSE,
        response.preedit[0] ? strlen(response.preedit) : 0, response.candidate_count,
        response.commit_text[0] ? strlen(response.commit_text) : 0, tsf_result_str(trace.result));

    if (trace.result == TsfResult::COMMITTED || trace.result == TsfResult::CLEARED) {
        _reset_stage_composition(trace.result == TsfResult::COMMITTED ? "commit" : "clear");
    }

    return *pfEaten != FALSE;
}

void TextService::_ProcessKeyUp(WPARAM wParam, LPARAM lParam) {
    if (wParam == VK_CAPITAL) {
        return;
    }

    _stageInputId = cxxime::stage_trace_input_id(static_cast<uint32_t>(wParam), lParam);

    // Config is reloaded by watcher thread (not keypress-driven).
    _config = get_config();

    uint32_t modifiers = _get_modifiers();
    CXXIME_LOG(L"_ProcessKeyUp: vk=%u, mods=%u, sessionId=%u", (unsigned int)wParam, modifiers,
               _sessionId);

    cxxime::IPCResponse response = {};
    uint32_t engine_calls = 0;
    auto process_key_up = [&]() {
        ++engine_calls;
        return _client.process_key(
            _sessionId, static_cast<uint32_t>(wParam), modifiers, response, true);
    };
    bool ok = _ensure_ipc_session() && process_key_up();
    if (ok && response.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        ok = false;
        if (_recreate_ipc_session_preserving_status()) {
            response = {};
            ok = process_key_up();
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
            ok = process_key_up();
        }
    }
    _ipcHealthy = ok;
    cxxime_tsf::trace_stage_key_route(
        _stageInputId, _stageCompositionId, static_cast<uint32_t>(wParam), engine_calls,
        ok ? "processed_key_up" : "ipc_failed_key_up");

    CXXIME_LOG(L"_ProcessKeyUp: ok=%d, ascii_mode=%d, commit='%S', composing=%d",
               ok, response.ascii_mode, response.commit_text, response.composing);

    bool committed = false;
    if (ok) {
        if (response.status == cxxime::IPCStatus::OK ||
            response.status == cxxime::IPCStatus::ERR_ENGINE_PROCESS_FAILED) {
            _sync_ime_status(response.ime_status);
        }
        CXXIME_LOG(L"_ProcessKeyUp: _chinese_mode=%d, _composing=%d", _chinese_mode, _composing);

        // Handle committed text from toggle (e.g. Shift with commit_text style)
        if (response.commit_text[0] != '\0') {
            std::wstring commit_text = utf8_to_wstring(response.commit_text);
            if (!commit_text.empty()) {
                ITfContext* pContext = _current_edit_context_for_composition();
                if (pContext) {
                    _commit_text(pContext, commit_text, true);
                    pContext->Release();
                } else {
                    insert_text(commit_text, true);
                }
                _composing = false;
                _lastInlineCompositionText.clear();
                _hide_candidate_window("hide:key_up_commit");
                _end_reading_ui_element("hide:key_up_commit_reading");
                _candidateWindow.set_preedit("");
                committed = true;
            }
        }
    }

    cxxime_tsf::trace_stage_key_result(
        _stageInputId, _stageCompositionId, static_cast<uint32_t>(wParam), false,
        response.preedit[0] ? strlen(response.preedit) : 0, response.candidate_count,
        response.commit_text[0] ? strlen(response.commit_text) : 0,
        committed ? "key_up_commit" : (ok ? "key_up" : "key_up_failed"));
    if (committed) {
        _reset_stage_composition("key_up_commit");
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

// ITfThreadFocusSink
STDMETHODIMP TextService::OnSetThreadFocus() {
    _update_input_focus_from_thread_mgr();
    _show_status_window_if_allowed("show:thread_focus");
    return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
    _inputFocused = false;
    _start_state_poll_timer();
    _hide_status_window("hide:thread_focus_lost");
    if (_sessionId && _client.is_connected())
        _client.focus_out(_sessionId);
    _AbortComposition();
    return S_OK;
}

void TextService::_AbortComposition() {
    _hide_candidate_window("hide:abort_composition");
    _end_reading_ui_element("hide:abort_composition_reading");
    _candidateWindow.set_preedit("");
    _lastInlineCompositionText.clear();
    if (_composing) {
        ITfContext* pContext = _current_edit_context_for_composition();
        if (pContext) {
            _end_composition(pContext);
            pContext->Release();
        }
        _composing = false;
    }
    _reset_stage_composition("abort");
}

void TextService::_reset_stage_composition(const char* reason) {
    cxxime_tsf::trace_stage_composition_end(_stageInputId, _stageCompositionId, reason);
    _stageCompositionId = 0;
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
    if (_inputFocused) {
        _advise_text_layout_sink(pDocMgrFocus);
    } else {
        _unadvise_text_layout_sink();
    }

    if (!_inputFocused) {
        _start_state_poll_timer();
        _hide_status_window("hide:document_focus_unfocused");
        _hide_candidate_window("hide:document_focus_unfocused");
        _end_reading_ui_element("hide:document_focus_unfocused_reading");
        _reset_stage_composition("document_unfocused");
        return S_OK;
    }

    _start_state_poll_timer();
    _show_status_window_if_allowed("show:document_focus");

    // Sync status on focus change (user may have toggled via language bar)
    cxxime::IPCResponse resp = {};
    if (_ensure_ipc_session() &&
        _client.get_status(_sessionId, resp) && resp.status == cxxime::IPCStatus::OK) {
        _sync_ime_status(resp.ime_status);
    }

    // Document focus changed; hide candidate window if switching away.
    if (_composing) {
        _hide_candidate_window("hide:document_focus_switch");
        _end_reading_ui_element("hide:document_focus_switch_reading");
        _candidateWindow.set_preedit("");
        if (_sessionId && _client.is_connected())
            _client.focus_out(_sessionId);
        // End composition in the previous context
        ITfContext* pContext = nullptr;
        if (_compositionContext) {
            pContext = _compositionContext;
            pContext->AddRef();
        } else if (pDocMgrPrevFocus) {
            pDocMgrPrevFocus->GetTop(&pContext);
        }
        if (pContext) {
            _end_composition(pContext);
            pContext->Release();
        }
        _composing = false;
        _reset_stage_composition("document_switch");
    }
    return S_OK;
}

STDMETHODIMP TextService::OnPushContext(ITfContext* pic) {
    return S_OK;
}

STDMETHODIMP TextService::OnPopContext(ITfContext* pic) {
    return S_OK;
}

STDMETHODIMP TextService::OnLayoutChange(ITfContext* pic,
                                          TfLayoutCode lcode,
                                          ITfContextView* view) {
    UNREFERENCED_PARAMETER(view);
    if (lcode != TF_LC_CHANGE)
        return S_OK;
    if (_textLayoutSinkContext && pic != _textLayoutSinkContext)
        return S_OK;

    if (_composing && _candidateWindow.is_visible()) {
        char detail[96] = {};
        snprintf(detail, sizeof(detail), "code=%d context_match=%d",
                 static_cast<int>(lcode), (_textLayoutSinkContext == pic) ? 1 : 0);
        _enqueue_event_trace("layout_change", detail);
    }
    _request_candidate_position_update(pic, "layout_change", true);
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

bool TextService::select_candidate_from_ui(UINT index) {
    cxxime::IPCResponse resp = {};
    if (!_ensure_ipc_session() || !_client.select_candidate(_sessionId, index, resp))
        return false;

    if (resp.status == cxxime::IPCStatus::ERR_INVALID_SESSION) {
        _recreate_ipc_session_preserving_status();
        _candidateWindow.set_preedit("");
        _hide_candidate_window("hide:select_invalid_session");
        _end_reading_ui_element("hide:select_invalid_session_reading");
        _composing = false;
        return false;
    }

    std::wstring commit_text = utf8_to_wstring(resp.commit_text);
    if (!commit_text.empty()) {
        ITfContext* pContext = _current_edit_context_for_composition();

        if (pContext) {
            _commit_text(pContext, commit_text, true);
            pContext->Release();
        } else {
            insert_text(commit_text, true);
        }
        _composing = false;
    }

    _candidateWindow.set_preedit("");
    _hide_candidate_window("hide:select_commit");
    _end_reading_ui_element("hide:select_commit_reading");
    return true;
}

void TextService::abort_candidate_ui_from_tsf() {
    if (_sessionId && _client.is_connected())
        _client.clear_composition(_sessionId);
    _AbortComposition();
}

HRESULT TextService::finalize_exact_candidate_ui_from_tsf() {
    std::wstring commit_text = _lastInlineCompositionText;
    if (commit_text.empty())
        return E_NOTIMPL;

    ITfContext* pContext = _current_edit_context_for_composition();

    HRESULT hr = S_OK;
    if (pContext) {
        hr = _commit_text(pContext, commit_text, true);
        pContext->Release();
    } else if (!commit_text.empty()) {
        hr = insert_text(commit_text, true);
    }
    if (_sessionId && _client.is_connected())
        _client.clear_composition(_sessionId);
    _candidateWindow.set_preedit("");
    _hide_candidate_window("hide:finalize_exact");
    _end_reading_ui_element("hide:finalize_exact_reading");
    _composing = false;
    _lastInlineCompositionText.clear();
    return hr;
}

void TextService::trace_ui_element_method(const char* element, const char* method, bool important) {
    char detail[96] = {};
    snprintf(detail, sizeof(detail), "%s.%s",
             element ? element : "unknown", method ? method : "unknown");
    _enqueue_event_trace("ui_element_call", detail, important);
}

uint64_t TextService::ensure_stage_composition_id() {
    if (_stageCompositionId == 0) {
        _stageCompositionId = cxxime::stage_trace_next_id();
    }
    return _stageCompositionId;
}

void TextService::trace_caret_event(const char* action,
                                    const char* source,
                                    bool resolved,
                                    const RECT* rect,
                                    HRESULT hr,
                                    bool important) {
    char detail[192] = {};
    if (rect) {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d rc=%ld,%ld,%ld,%ld hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, rect->left, rect->top, rect->right, rect->bottom,
                 static_cast<unsigned long>(hr), _composing ? 1 : 0,
                 _candidateWindow.is_visible() ? 1 : 0);
    } else {
        snprintf(detail, sizeof(detail),
                 "action=%s source=%s resolved=%d hr=0x%08lx composing=%d visible=%d",
                 action ? action : "unknown", source ? source : "unknown",
                 resolved ? 1 : 0, static_cast<unsigned long>(hr),
                 _composing ? 1 : 0, _candidateWindow.is_visible() ? 1 : 0);
    }
    _enqueue_event_trace("caret_position", detail, important);
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

bool TextService::_register_display_attribute_atom() {
    ITfCategoryMgr* category_mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfCategoryMgr, reinterpret_cast<void**>(&category_mgr));
    if (FAILED(hr) || !category_mgr)
        return false;
    
    hr = category_mgr->RegisterGUID(c_guidDisplayAttribute, &_displayAttributeAtom);
    category_mgr->Release();
    if (FAILED(hr)) {
        _displayAttributeAtom = 0;
        CXXIME_LOG(L"Register display attribute atom failed: hr=0x%08x", hr);
        return false;
    }

    return true;
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

bool TextService::_resolve_native_caret_rect(RECT* out) const {
    if (!out)
        return false;
    GUITHREADINFO gti = { sizeof(gti) };
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (foreground_thread &&
        GetGUIThreadInfo(foreground_thread, &gti) &&
        gti.hwndCaret &&
        !is_top_level_window(gti.hwndCaret) &&
        same_root_window(foreground, gti.hwndCaret)) {
        RECT rc = gti.rcCaret;
        POINT points[2] = {
            { rc.left, rc.top },
            { rc.right, rc.bottom },
        };
        MapWindowPoints(gti.hwndCaret, nullptr, points, 2);
        SetRect(&rc, points[0].x, points[0].y, points[1].x, points[1].y);
        normalize_caret_rect_size(&rc);
        if (is_valid_caret_rect(rc)) {
            *out = rc;
            return true;
        }
    }

    POINT pt = {};
    if (GetCaretPos(&pt)) {
        HWND focus = GetFocus();
        if (!focus && gti.hwndFocus)
            focus = gti.hwndFocus;
        if (focus && same_root_window(foreground, focus)) {
            ClientToScreen(focus, &pt);
            RECT rc = {};
            SetRect(&rc, pt.x, pt.y, pt.x + 1, pt.y + 20);
            if (is_valid_caret_rect(rc)) {
                *out = rc;
                return true;
            }
        }
    }

    return false;
}

bool TextService::_resolve_context_native_caret_rect(ITfContext* context, RECT* out) const {
    if (!context || !out)
        return false;

    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view)
        return false;

    HWND context_hwnd = nullptr;
    HRESULT hr = view->GetWnd(&context_hwnd);
    view->Release();
    if (FAILED(hr) || !context_hwnd)
        return false;
    if (is_top_level_window(context_hwnd))
        return false;

    GUITHREADINFO gti = { sizeof(gti) };
    DWORD context_thread = GetWindowThreadProcessId(context_hwnd, nullptr);
    if (!context_thread || !GetGUIThreadInfo(context_thread, &gti) || !gti.hwndCaret)
        return false;

    // Classic HWND-backed editors can expose a fresher Win32 caret than TSF GetTextExt
    // immediately after Enter/newline. TSF-only framework hosts can report a fake caret
    // elsewhere in the same top-level window; only trust a caret owned by the active
    // context view itself or one of its descendants.
    if (is_top_level_window(gti.hwndCaret))
        return false;
    if (gti.hwndCaret != context_hwnd && !IsChild(context_hwnd, gti.hwndCaret))
        return false;

    RECT rc = gti.rcCaret;
    POINT points[2] = {
        { rc.left, rc.top },
        { rc.right, rc.bottom },
    };
    MapWindowPoints(gti.hwndCaret, nullptr, points, 2);
    SetRect(&rc, points[0].x, points[0].y, points[1].x, points[1].y);
    normalize_caret_rect_size(&rc);
    if (!is_valid_caret_rect(rc))
        return false;

    *out = rc;
    return true;
}

RECT TextService::_resolve_caret_rect(ITfContext* pic) {
    (void)pic;
    RECT rc = {};

    if (_resolve_native_caret_rect(&rc))
        return rc;

    if (is_valid_caret_rect(_caretRect)) {
        return _caretRect;
    }

    POINT pt = {};
    if (GetCursorPos(&pt)) {
        SetRect(&rc, pt.x, pt.y, pt.x, pt.y + 20);
        return rc;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && GetWindowRect(foreground, &rc)) {
        LONG x = rc.left + 24;
        LONG y_offset = (rc.bottom - rc.top) * 2 / 3;
        if (y_offset < 24)
            y_offset = 24;
        LONG y = rc.top + y_offset;
        SetRect(&rc, x, y, x, y + 20);
        return rc;
    }

    return rc;
}
