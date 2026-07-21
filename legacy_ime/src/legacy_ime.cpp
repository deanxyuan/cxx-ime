// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "legacy_common.h"
#include "legacy_session.h"
#include "legacy_ui.h"
#include "legacy_stage_diagnostics.h"

#include <cxxime/stage_trace.h>

#include <cstring>
#include <memory>

namespace {

HINSTANCE g_instance = nullptr;
bool g_winlogon = false;

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
        cxxime_legacy::register_ui_class(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        cxxime_legacy::clear_sessions(false);
        if (g_instance) {
            cxxime_legacy::unregister_ui_class(g_instance);
        }
    }
    return TRUE;
}

extern "C" BOOL WINAPI ImeInquire(LPIMEINFO ime_info, LPTSTR ui_class, DWORD system_info_flags) {
    cxxime_legacy::trace_stage_legacy_inquire(
        system_info_flags, ime_info != nullptr && ui_class != nullptr);
    if (!ime_info || !ui_class) {
        return FALSE;
    }

    g_winlogon = (system_info_flags & IME_SYSINFO_WINLOGON) != 0;

    std::memset(ime_info, 0, sizeof(*ime_info));
    ime_info->dwPrivateDataSize = 0;
    ime_info->fdwProperty = IME_PROP_UNICODE | IME_PROP_AT_CARET;
    ime_info->fdwConversionCaps = IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE;
    ime_info->fdwSentenceCaps = IME_SMODE_NONE;
    ime_info->fdwUICaps = UI_CAP_2700;
    ime_info->fdwSCSCaps = 0;
    ime_info->fdwSelectCaps = SELECT_CAP_CONVERSION;

    wcscpy_s(ui_class, IME_UI_CLASS_NAME_SIZE, cxxime_legacy::kUiClassName);
    return TRUE;
}

extern "C" BOOL WINAPI ImeSelect(HIMC himc, BOOL select) {
    cxxime_legacy::trace_stage_legacy_select(himc, select != FALSE);
    if (g_winlogon) {
        return TRUE;
    }

    if (select) {
        std::shared_ptr<cxxime_legacy::LegacyImeSession> session =
            cxxime_legacy::find_session(himc, true);
        if (session) {
            session->select(true);
        }
        return TRUE;
    }

    std::shared_ptr<cxxime_legacy::LegacyImeSession> session =
        cxxime_legacy::find_session(himc, false);
    if (session) {
        session->select(false);
    }
    cxxime_legacy::remove_session(himc);
    return TRUE;
}

extern "C" BOOL WINAPI ImeSetActiveContext(HIMC himc, BOOL active) {
    cxxime_legacy::trace_stage_legacy_active_context(himc, active != FALSE);
    if (g_winlogon) {
        return TRUE;
    }

    std::shared_ptr<cxxime_legacy::LegacyImeSession> session =
        cxxime_legacy::find_session(himc, active != FALSE);
    if (session) {
        session->set_active(active != FALSE);
    }
    return TRUE;
}

extern "C" BOOL WINAPI ImeProcessKey(HIMC himc, UINT virtual_key, LPARAM key_data,
                                     CONST LPBYTE key_state) {
    const uint64_t input_id = cxxime::stage_trace_input_id(virtual_key, key_data);
    if (g_winlogon) {
        cxxime_legacy::trace_stage_legacy_process_key(
            himc, input_id, virtual_key, key_data, 0, false, "winlogon_rejected", false);
        return FALSE;
    }

    std::shared_ptr<cxxime_legacy::LegacyImeSession> session =
        cxxime_legacy::find_session(himc, true);
    if (!session) {
        cxxime_legacy::trace_stage_legacy_process_key(
            himc, input_id, virtual_key, key_data, 0, false, "no_session", false);
        return FALSE;
    }
    const BOOL eaten = session->process_key(virtual_key, key_data, key_state) ? TRUE : FALSE;
    cxxime_legacy::trace_stage_legacy_process_key(
        himc, input_id, virtual_key, key_data, session->last_engine_calls(), eaten != FALSE,
        eaten ? "processed_eaten" : "processed_passed", true);
    return eaten;
}

extern "C" UINT WINAPI ImeToAsciiEx(UINT virtual_key,
                                    UINT scan_code,
                                    CONST LPBYTE,
                                    LPTRANSMSGLIST,
                                    UINT state,
                                    HIMC himc) {
    cxxime_legacy::trace_stage_legacy_to_ascii(
        himc, cxxime::stage_trace_input_id(virtual_key, scan_code), virtual_key, scan_code, state);
    return 0;
}

extern "C" BOOL WINAPI NotifyIME(HIMC himc, DWORD action, DWORD index, DWORD value) {
    cxxime_legacy::trace_stage_legacy_notify(himc, action, index, value);
    if (g_winlogon) {
        return TRUE;
    }

    std::shared_ptr<cxxime_legacy::LegacyImeSession> session =
        cxxime_legacy::find_session(himc, false);
    if (!session) {
        return TRUE;
    }

    switch (action) {
    case NI_CLOSECANDIDATE:
        session->close_candidate_list();
        break;
    case NI_COMPOSITIONSTR:
        if (index == CPS_CANCEL || index == CPS_REVERT) {
            session->cancel_composition();
        } else if (index == CPS_COMPLETE) {
            session->complete_composition();
        }
        break;
    case NI_SELECTCANDIDATESTR:
        if (index == cxxime_legacy::kCandidateListIndex) {
            session->select_candidate(value);
        }
        break;
    case NI_SETCANDIDATE_PAGESTART:
        if (index == cxxime_legacy::kCandidateListIndex) {
            session->set_candidate_page_start(value);
        }
        break;
    case NI_SETCANDIDATE_PAGESIZE:
        if (index == cxxime_legacy::kCandidateListIndex) {
            session->set_candidate_page_size(value);
        }
        break;
    case NI_FINALIZECONVERSIONRESULT:
        session->complete_composition();
        break;
    case NI_CONTEXTUPDATED:
        if (index == IMC_SETOPENSTATUS) {
            session->handle_open_status_changed();
        }
        break;
    default:
        break;
    }
    return TRUE;
}

extern "C" BOOL WINAPI ImeConfigure(HKL, HWND parent, DWORD mode, LPVOID) {
    if (g_winlogon) {
        return TRUE;
    }

    if (mode == IME_CONFIG_GENERAL ||
        mode == IME_CONFIG_REGISTERWORD ||
        mode == IME_CONFIG_SELECTDICTIONARY) {
        return cxxime_legacy::launch_settings(parent) ? TRUE : FALSE;
    }
    return TRUE;
}

extern "C" LRESULT WINAPI ImeEscape(HIMC, UINT, LPVOID) {
    return 0;
}

extern "C" DWORD WINAPI ImeConversionList(HIMC, LPCTSTR, LPCANDIDATELIST, DWORD, UINT) {
    return 0;
}

extern "C" BOOL WINAPI ImeRegisterWord(LPCTSTR, DWORD, LPCTSTR) {
    return FALSE;
}

extern "C" BOOL WINAPI ImeUnregisterWord(LPCTSTR, DWORD, LPCTSTR) {
    return FALSE;
}

extern "C" UINT WINAPI ImeGetRegisterWordStyle(UINT, LPSTYLEBUF) {
    return 0;
}

extern "C" UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROC, LPCTSTR, DWORD, LPCTSTR, LPVOID) {
    return 0;
}

extern "C" BOOL WINAPI ImeSetCompositionString(HIMC, DWORD, LPVOID, DWORD, LPVOID, DWORD) {
    return FALSE;
}

extern "C" BOOL WINAPI ImeDestroy(UINT) {
    cxxime_legacy::trace_stage_legacy_destroy();
    cxxime_legacy::clear_sessions(true);
    return TRUE;
}
