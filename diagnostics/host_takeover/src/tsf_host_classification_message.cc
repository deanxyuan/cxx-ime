// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_classification_message.h"
#include "tsf_host_ime_private_api.h"
#include "tsf_imm_mode.h"

#include "host_compatibility/host_classification_compatibility.h"
#include "host_compatibility/host_ime_private_api.h"

#include <cxxime/host_trace.h>

#include <imm.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

namespace cxxime_tsf {
namespace {

constexpr uintptr_t kInputSystemManagerRva = 0x444c0;
constexpr uintptr_t kInputSystemImeEnabledRva = 0x45bf2;
constexpr uintptr_t kInputSystemCallbackRva = 0x31a0;
constexpr uintptr_t kInputSystemUserdataRva = 0x45ba0;

constexpr uint32_t kUnsupportedInputMethodCode = 0x01000000;
constexpr uint32_t kDefaultInputSourceCode = 0x02000000;
constexpr char kImeManagerInterfaceName[] = "IMEManager001";

using CreateInterfaceFn = void* (*)(const char* name, int* return_code);

struct ModuleIdentity {
    bool readable = false;
    DWORD timestamp = 0;
    DWORD image_size = 0;
};

template <typename Value>
bool read_process_value(const void* address, Value* value) {
    SIZE_T bytes_read = 0;
    return address && value &&
        ReadProcessMemory(
            GetCurrentProcess(), address, value, sizeof(*value), &bytes_read) != FALSE &&
        bytes_read == sizeof(*value);
}

template <typename Value>
bool read_at(uintptr_t base, uintptr_t offset, Value* value) {
    return base != 0 &&
        read_process_value(reinterpret_cast<const void*>(base + offset), value);
}

ModuleIdentity read_module_identity(HMODULE module) {
    ModuleIdentity identity;
    IMAGE_DOS_HEADER dos = {};
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    if (!read_at(base, 0, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0) {
        return identity;
    }

    IMAGE_NT_HEADERS headers = {};
    if (!read_at(base, static_cast<uintptr_t>(dos.e_lfanew), &headers) ||
        headers.Signature != IMAGE_NT_SIGNATURE) {
        return identity;
    }

    identity.readable = true;
    identity.timestamp = headers.FileHeader.TimeDateStamp;
    identity.image_size = headers.OptionalHeader.SizeOfImage;
    return identity;
}

bool has_value(uint32_t value, std::initializer_list<uint32_t> values) {
    for (uint32_t item : values) {
        if (value == item) {
            return true;
        }
    }
    return false;
}

const char* candidate_input_method_branch(uint32_t code, WPARAM command) {
    const bool shared_specialized = has_value(code, {
        0x00003000,
        0x00060000,
        0x00a10000,
        0x00d00800,
        0x00d00900,
        0x00d00a00,
    });
    if (command == IMN_CHANGECANDIDATE) {
        if (has_value(code, {0x00210000, 0x00110000, 0x00810000, 0x00d10000})) {
            return "specialized_stop";
        }
        return shared_specialized ? "specialized_then_base" : "base_only";
    }
    if (command == IMN_OPENCANDIDATE) {
        if (has_value(code, {0x00210000, 0x00110000, 0x00d10000})) {
            return "specialized_stop";
        }
        if (code == 0x00810000 || shared_specialized) {
            return "specialized_then_base";
        }
        return "base_only";
    }
    return "not_applicable";
}

const char* candidate_processor_kind(uintptr_t notify_method,
                                     uintptr_t imemanager_base,
                                     DWORD imemanager_image_size) {
    return host_ime_candidate_notify_method_matches(
            imemanager_base, imemanager_image_size, notify_method)
        ? "specialized_candidate"
        : "other";
}

uintptr_t module_rva(uintptr_t module, uintptr_t address, DWORD image_size) {
    return module && address >= module && address < module + image_size
        ? address - module
        : 0;
}

void add_module_fields(nlohmann::json& fields,
                       const char* prefix,
                       HMODULE module,
                       const ModuleIdentity& identity) {
    const std::string name(prefix);
    fields[name + "_loaded"] = module != nullptr;
    fields[name + "_base"] = reinterpret_cast<uintptr_t>(module);
    fields[name + "_identity_readable"] = identity.readable;
    fields[name + "_timestamp"] = identity.timestamp;
    fields[name + "_image_size"] = identity.image_size;
}

void inspect_host_classification_message_gate(const MSG& message,
                                              const char* trigger) {
    if (message.message != WM_IME_NOTIFY ||
        (message.wParam != IMN_OPENCANDIDATE &&
         message.wParam != IMN_CHANGECANDIDATE &&
         message.wParam != IMN_CLOSECANDIDATE)) {
        return;
    }

    HMODULE inputsystem = GetModuleHandleW(L"inputsystem.dll");
    HMODULE imemanager = GetModuleHandleW(L"imemanager.dll");
    const ModuleIdentity inputsystem_identity = read_module_identity(inputsystem);
    const ModuleIdentity imemanager_identity = read_module_identity(imemanager);
    FARPROC create_interface_export = imemanager
        ? GetProcAddress(imemanager, "CreateInterface")
        : nullptr;
    int create_interface_return_code = -1;
    void* ime_manager_interface = create_interface_export
        ? reinterpret_cast<CreateInterfaceFn>(create_interface_export)(
              kImeManagerInterfaceName, &create_interface_return_code)
        : nullptr;
    uintptr_t ime_manager_interface_vtable = 0;
    const bool ime_manager_interface_vtable_read = read_at(
        reinterpret_cast<uintptr_t>(ime_manager_interface),
        0,
        &ime_manager_interface_vtable);
    nlohmann::json fields = {
        {"message", message.message},
        {"command", static_cast<uint64_t>(message.wParam)},
        {"candidate_list_mask", static_cast<uint64_t>(message.lParam)},
        {"hwnd", reinterpret_cast<uintptr_t>(message.hwnd)},
        {"inputsystem_callback_rva", kInputSystemCallbackRva},
        {"trigger", trigger},
    };
    fields["create_interface_export_present"] =
        create_interface_export != nullptr;
    fields["ime_manager_interface_name"] = kImeManagerInterfaceName;
    fields["ime_manager_interface_return_code"] =
        create_interface_return_code;
    fields["ime_manager_interface"] =
        reinterpret_cast<uintptr_t>(ime_manager_interface);
    fields["ime_manager_interface_vtable_read"] =
        ime_manager_interface_vtable_read;
    fields["ime_manager_interface_vtable"] = ime_manager_interface_vtable;
    fields["ime_manager_interface_vtable_rva"] = module_rva(
        reinterpret_cast<uintptr_t>(imemanager),
        ime_manager_interface_vtable,
        imemanager_identity.image_size);
    const bool ime_manager_interface_ready =
        create_interface_export != nullptr &&
        create_interface_return_code == 0 &&
        ime_manager_interface != nullptr &&
        ime_manager_interface_vtable_read;
    fields["ime_manager_interface_ready"] = ime_manager_interface_ready;
    fields["compatibility_manager_source"] = "create_interface";
    fields["inputsystem_gate_required"] = false;
    const TraceImmProfileSnapshot imm_profile =
        capture_imm_profile(message.hwnd);
    fields["window_thread_id"] = imm_profile.window_thread_id;
    fields["keyboard_layout"] = imm_profile.keyboard_layout;
    fields["keyboard_layout_language"] = imm_profile.keyboard_layout_language;
    fields["keyboard_layout_device"] = imm_profile.keyboard_layout_device;
    fields["keyboard_layout_is_ime"] = imm_profile.keyboard_layout_is_ime;
    fields["ime_file_name_length"] = imm_profile.ime_file_name_length;
    fields["ime_file_name"] = imm_profile.ime_file_name;
    fields["imm_property"] = imm_profile.property;
    fields["imm_conversion_property"] = imm_profile.conversion;
    fields["imm_sentence_property"] = imm_profile.sentence;
    fields["imm_ui_property"] = imm_profile.ui;
    fields["imm_set_composition_string_property"] =
        imm_profile.set_composition_string;
    fields["imm_select_property"] = imm_profile.select;
    fields["imm_ime_version"] = imm_profile.ime_version;
    add_module_fields(fields, "inputsystem", inputsystem, inputsystem_identity);
    add_module_fields(fields, "imemanager", imemanager, imemanager_identity);

#if !defined(_M_X64)
    fields["result"] = "architecture_unsupported";
    cxxime::write_host_trace(
        "tsf", "host.classification_message_gate", std::move(fields));
    return;
#else
    const bool imemanager_identity_ready = imemanager_identity.readable;
    if (!imemanager_identity_ready) {
        fields["result"] = "module_identity_unavailable";
        cxxime::write_host_trace(
            "tsf", "host.classification_message_gate", std::move(fields));
        return;
    }

    const uintptr_t inputsystem_base = reinterpret_cast<uintptr_t>(inputsystem);
    const uintptr_t imemanager_base = reinterpret_cast<uintptr_t>(imemanager);
    const bool inputsystem_layout_probe_available =
        inputsystem_identity.readable &&
        inputsystem_identity.image_size > kInputSystemImeEnabledRva;
    fields["inputsystem_layout_probe_available"] =
        inputsystem_layout_probe_available;
    fields["inputsystem_expected_userdata"] =
        inputsystem_base + kInputSystemUserdataRva;
    uintptr_t inputsystem_manager = 0;
    uint8_t inputsystem_ime_enabled = 0;
    const bool inputsystem_manager_read =
        inputsystem_layout_probe_available && read_at(
                                                      inputsystem_base, kInputSystemManagerRva, &inputsystem_manager);
    const bool inputsystem_enabled_read =
        inputsystem_layout_probe_available && read_at(
                                                      inputsystem_base, kInputSystemImeEnabledRva,
                                                      &inputsystem_ime_enabled);
    fields["inputsystem_manager_read"] = inputsystem_manager_read;
    fields["inputsystem_manager"] = inputsystem_manager;
    fields["ime_manager_interface_matches_inputsystem_manager"] =
        ime_manager_interface != nullptr &&
        reinterpret_cast<uintptr_t>(ime_manager_interface) == inputsystem_manager;
    fields["inputsystem_ime_enabled_read"] = inputsystem_enabled_read;
    fields["inputsystem_ime_enabled"] = inputsystem_ime_enabled != 0;
    const bool inputsystem_gate_ready =
        inputsystem_manager_read && inputsystem_manager != 0 &&
        inputsystem_enabled_read &&
        inputsystem_ime_enabled != 0;
    fields["inputsystem_gate_ready"] = inputsystem_gate_ready;
    if (!ime_manager_interface_ready) {
        fields["result"] = "manager_unavailable";
        cxxime::write_host_trace(
            "tsf", "host.classification_message_gate", std::move(fields));
        return;
    }

    const uintptr_t manager =
        reinterpret_cast<uintptr_t>(ime_manager_interface);
    uintptr_t manager_vtable = 0;
    uintptr_t manager_method = 0;
    uintptr_t window_handler = 0;
    uint8_t manager_gate_60 = 0;
    uint8_t manager_gate_61 = 0;
    const bool manager_vtable_read = read_at(manager, 0, &manager_vtable);
    const bool window_handler_read = read_at(manager, 0x58, &window_handler);
    const bool manager_gate_60_read = read_at(manager, 0x60, &manager_gate_60);
    const bool manager_gate_61_read = read_at(manager, 0x61, &manager_gate_61);
    const bool manager_method_read = read_at(manager_vtable, 0x80, &manager_method);
    const bool manager_state_read =
        manager_vtable_read && window_handler_read && manager_method_read;
    fields["manager_state_read"] = manager_state_read;
    fields["manager_vtable_read"] = manager_vtable_read;
    fields["window_handler_read"] = window_handler_read;
    fields["manager_gate_60_read"] = manager_gate_60_read;
    fields["manager_gate_61_read"] = manager_gate_61_read;
    fields["manager_method_read"] = manager_method_read;
    fields["manager_vtable"] = manager_vtable;
    fields["manager_vtable_rva"] = module_rva(
        imemanager_base, manager_vtable, imemanager_identity.image_size);
    fields["manager_message_method_rva"] = module_rva(
        imemanager_base, manager_method, imemanager_identity.image_size);
    fields["manager_message_method_matches"] =
        host_ime_manager_message_method_matches(
            imemanager_base, imemanager_identity.image_size, manager_method);
    fields["manager_gate_60"] = manager_gate_60 != 0;
    fields["manager_gate_61"] = manager_gate_61 != 0;
    fields["window_handler"] = window_handler;
    if (!manager_state_read || window_handler == 0) {
        fields["result"] = "manager_state_unavailable";
        cxxime::write_host_trace(
            "tsf", "host.classification_message_gate", std::move(fields));
        return;
    }

    uintptr_t window_vtable = 0;
    uintptr_t window_message_method = 0;
    uintptr_t candidate_processor = 0;
    uintptr_t auxiliary_input_handler = 0;
    HWND active_hwnd = nullptr;
    uint8_t window_message_enabled = 0;
    const bool window_vtable_read = read_at(window_handler, 0, &window_vtable);
    const bool active_hwnd_read = read_at(window_handler, 0x20, &active_hwnd);
    const bool candidate_processor_read = read_at(
        window_handler, 0x28, &candidate_processor);
    const bool auxiliary_input_handler_read = read_at(
        window_handler, 0x30, &auxiliary_input_handler);
    const bool window_message_enabled_read = read_at(
        window_handler, 0x51, &window_message_enabled);
    const bool window_message_method_read = read_at(
        window_vtable, 0xc0, &window_message_method);
    const bool window_state_read =
        window_vtable_read && active_hwnd_read && candidate_processor_read &&
        window_message_enabled_read && window_message_method_read;
    fields["window_state_read"] = window_state_read;
    fields["window_vtable_read"] = window_vtable_read;
    fields["active_hwnd_read"] = active_hwnd_read;
    fields["candidate_processor_read"] = candidate_processor_read;
    fields["auxiliary_input_handler_read"] = auxiliary_input_handler_read;
    fields["window_message_enabled_read"] = window_message_enabled_read;
    fields["window_message_method_read"] = window_message_method_read;
    fields["window_vtable"] = window_vtable;
    fields["window_vtable_rva"] = module_rva(
        imemanager_base, window_vtable, imemanager_identity.image_size);
    fields["window_message_method_rva"] = module_rva(
        imemanager_base, window_message_method, imemanager_identity.image_size);
    fields["window_message_method_matches"] =
        host_ime_window_message_method_matches(
            imemanager_base, imemanager_identity.image_size,
            window_message_method);
    fields["active_hwnd"] = reinterpret_cast<uintptr_t>(active_hwnd);
    fields["active_hwnd_matches"] = active_hwnd == message.hwnd;
    fields["window_message_enabled"] = window_message_enabled != 0;
    fields["candidate_processor"] = candidate_processor;
    fields["auxiliary_input_handler"] = auxiliary_input_handler;
    const bool window_gate_ready =
        window_state_read &&
        host_ime_window_message_method_matches(
            imemanager_base, imemanager_identity.image_size,
            window_message_method) &&
        active_hwnd == message.hwnd &&
        window_message_enabled != 0;
    fields["window_gate_ready"] = window_gate_ready;
    if (!window_state_read || candidate_processor == 0) {
        fields["result"] = "window_state_unavailable";
        cxxime::write_host_trace(
            "tsf", "host.classification_message_gate", std::move(fields));
        return;
    }

    uintptr_t auxiliary_input_vtable = 0;
    uintptr_t auxiliary_classification_method = 0;
    uintptr_t auxiliary_input_method = 0;
    uint32_t auxiliary_input_source_code = 0;
    uint32_t auxiliary_profile_code = 0;
    const bool auxiliary_input_vtable_read = read_at(
        auxiliary_input_handler, 0, &auxiliary_input_vtable);
    const bool auxiliary_classification_method_read = read_at(
        auxiliary_input_vtable, 0x10, &auxiliary_classification_method);
    const bool auxiliary_input_method_read = read_at(
        auxiliary_input_vtable, 0x30, &auxiliary_input_method);
    const bool auxiliary_input_source_code_read = read_at(
        auxiliary_input_handler, 0xd0, &auxiliary_input_source_code);
    const bool auxiliary_profile_code_read = read_at(
        auxiliary_input_handler, 0xd4, &auxiliary_profile_code);
    const uint32_t raw_effective_classification_code =
        auxiliary_input_source_code == kDefaultInputSourceCode
        ? kUnsupportedInputMethodCode
        : auxiliary_profile_code;
    fields["auxiliary_input_vtable_read"] = auxiliary_input_vtable_read;
    fields["auxiliary_classification_method_read"] =
        auxiliary_classification_method_read;
    fields["auxiliary_input_method_read"] = auxiliary_input_method_read;
    fields["auxiliary_input_source_code_read"] = auxiliary_input_source_code_read;
    fields["auxiliary_profile_code_read"] = auxiliary_profile_code_read;
    fields["auxiliary_input_vtable_rva"] = module_rva(
        imemanager_base, auxiliary_input_vtable, imemanager_identity.image_size);
    fields["auxiliary_classification_method_rva"] = module_rva(
        imemanager_base,
        auxiliary_classification_method,
        imemanager_identity.image_size);
    fields["auxiliary_classification_method_matches"] =
        host_ime_classification_method_matches(
            imemanager_base, imemanager_identity.image_size,
            auxiliary_classification_method);
    fields["auxiliary_input_method_rva"] = module_rva(
        imemanager_base, auxiliary_input_method, imemanager_identity.image_size);
    fields["auxiliary_input_source_code"] = auxiliary_input_source_code;
    fields["auxiliary_profile_code"] = auxiliary_profile_code;
    fields["raw_effective_classification_code"] =
        raw_effective_classification_code;
    const HostImePrivateApiRequest private_api_request = {
        imemanager_base,
        imemanager_identity.image_size,
        manager,
        auxiliary_input_handler,
        imemanager_identity_ready,
        manager_gate_60_read,
        manager_gate_60 != 0,
        manager_gate_61_read,
        manager_gate_61 != 0,
        auxiliary_input_source_code_read && auxiliary_profile_code_read,
        raw_effective_classification_code,
    };
    const HostImePrivateApiSnapshot private_api =
        inspect_host_ime_private_api(private_api_request);
    add_host_ime_private_api_fields(
        fields, private_api_request, private_api);
    const uint32_t effective_classification_code =
        private_api.classification_called
        ? private_api.classification
        : raw_effective_classification_code;
    fields["effective_classification_code"] = effective_classification_code;
    const bool manager_gate_ready =
        manager_state_read &&
        host_ime_manager_message_method_matches(
            imemanager_base, imemanager_identity.image_size, manager_method) &&
        private_api.manager_called && private_api.manager_initialized &&
        private_api.manager_enabled;
    fields["manager_gate_source"] = "private_api";
    fields["manager_gate_ready"] = manager_gate_ready;

    uintptr_t candidate_vtable = 0;
    uintptr_t candidate_notify_method = 0;
    uintptr_t candidate_change_method = 0;
    uintptr_t candidate_open_method = 0;
    uint32_t candidate_input_method_code = 0;
    const bool candidate_vtable_read = read_at(
        candidate_processor, 0, &candidate_vtable);
    const bool candidate_input_method_code_read = read_at(
        candidate_processor, 0x68, &candidate_input_method_code);
    const bool candidate_notify_method_read = read_at(
        candidate_vtable, 0x30, &candidate_notify_method);
    const bool candidate_change_method_read = read_at(
        candidate_vtable, 0xd8, &candidate_change_method);
    const bool candidate_open_method_read = read_at(
        candidate_vtable, 0xe0, &candidate_open_method);
    const bool candidate_state_read =
        candidate_vtable_read && candidate_input_method_code_read &&
        candidate_notify_method_read && candidate_change_method_read &&
        candidate_open_method_read;
    const bool specialized_candidate_processor =
        host_ime_candidate_notify_method_matches(
            imemanager_base, imemanager_identity.image_size,
            candidate_notify_method);
    const char* input_method_branch = specialized_candidate_processor
        ? candidate_input_method_branch(candidate_input_method_code, message.wParam)
        : "not_specialized_processor";
    char input_method_code_hex[16] = {};
    snprintf(input_method_code_hex, sizeof(input_method_code_hex),
             "0x%08x", candidate_input_method_code);
    fields["candidate_state_read"] = candidate_state_read;
    fields["candidate_vtable_read"] = candidate_vtable_read;
    fields["candidate_input_method_code_read"] = candidate_input_method_code_read;
    fields["candidate_notify_method_read"] = candidate_notify_method_read;
    fields["candidate_change_method_read"] = candidate_change_method_read;
    fields["candidate_open_method_read"] = candidate_open_method_read;
    fields["candidate_vtable"] = candidate_vtable;
    fields["candidate_vtable_rva"] = module_rva(
        imemanager_base, candidate_vtable, imemanager_identity.image_size);
    fields["candidate_notify_method_rva"] = module_rva(
        imemanager_base, candidate_notify_method, imemanager_identity.image_size);
    fields["candidate_processor_kind"] = candidate_processor_kind(
        candidate_notify_method, imemanager_base,
        imemanager_identity.image_size);
    fields["candidate_change_method_rva"] = module_rva(
        imemanager_base, candidate_change_method, imemanager_identity.image_size);
    fields["candidate_open_method_rva"] = module_rva(
        imemanager_base, candidate_open_method, imemanager_identity.image_size);
    const bool candidate_methods_match =
        candidate_state_read && host_ime_candidate_methods_match(
                                                                 imemanager_base, imemanager_identity.image_size,
                                                                 candidate_notify_method, candidate_change_method,
                                                                 candidate_open_method);
    const bool all_pre_candidate_gates_ready =
        ime_manager_interface_ready && manager_gate_ready && window_gate_ready;
    const HostClassificationCompatibilitySnapshot& compatibility =
        host_classification_compatibility_snapshot();
    fields["candidate_methods_match"] = candidate_methods_match;
    fields["candidate_input_method_code"] = candidate_input_method_code;
    fields["candidate_input_method_code_hex"] = input_method_code_hex;
    fields["candidate_input_method_branch"] = input_method_branch;
    fields["classification_matches_candidate"] =
        auxiliary_input_source_code_read && auxiliary_profile_code_read &&
        effective_classification_code == candidate_input_method_code;
    fields["host_message_dispatch_expected"] =
        candidate_input_method_code_read &&
        candidate_input_method_code != kUnsupportedInputMethodCode;
    fields["compatibility_active"] = compatibility.active;
    fields["compatibility_attempted"] = compatibility.attempted;
    fields["compatibility_write_succeeded"] = compatibility.write_succeeded;
    fields["compatibility_readback_succeeded"] = compatibility.readback_succeeded;
    fields["compatibility_requested_profile_code"] =
        compatibility.requested_profile_code;
    fields["compatibility_readback_profile_code"] =
        compatibility.readback_profile_code;
    fields["compatibility_win32_error"] = compatibility.win32_error;
    fields["compatibility_result"] = compatibility.result;
    fields["host_dispatch_after_compatibility_expected"] =
        compatibility.readback_succeeded &&
        compatibility.readback_profile_code != kUnsupportedInputMethodCode;
    fields["candidate_imm_preprocess_expected"] =
        candidate_methods_match &&
        input_method_branch != std::string("base_only") &&
        input_method_branch != std::string("not_applicable") &&
        input_method_branch != std::string("not_specialized_processor");
    fields["all_pre_candidate_gates_ready"] = all_pre_candidate_gates_ready;
    fields["result"] = candidate_state_read ? "captured" : "candidate_state_unavailable";
    cxxime::write_host_trace(
        "tsf", "host.classification_message_gate", std::move(fields));
#endif
}

} // namespace

void preflight_host_classification_compatibility(HWND hwnd) {
    MSG message = {};
    message.hwnd = hwnd;
    message.message = WM_IME_NOTIFY;
    message.wParam = IMN_OPENCANDIDATE;
    message.lParam = 1;
    inspect_host_classification_message_gate(message, "candidate_preflight");
}

void trace_host_classification_message_gate(const MSG& message) {
    inspect_host_classification_message_gate(message, "queued_message");
}

} // namespace cxxime_tsf
