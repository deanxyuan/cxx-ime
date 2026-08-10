// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_host_ime_private_api.h"

namespace cxxime_tsf {

void add_host_ime_private_api_fields(
                                     nlohmann::json& fields,
                                     const HostImePrivateApiRequest& request,
                                     const HostImePrivateApiSnapshot& snapshot) {
    fields["private_api_architecture_supported"] =
        snapshot.architecture_supported;
    fields["private_api_manager_vtable_read"] = snapshot.manager_vtable_read;
    fields["private_api_manager_vtable_rva"] = snapshot.manager_vtable_rva;
    fields["private_api_manager_initialized_method_read"] =
        snapshot.manager_initialized_method_read;
    fields["private_api_manager_initialized_method_rva"] =
        snapshot.manager_initialized_method_rva;
    fields["private_api_manager_enabled_method_read"] =
        snapshot.manager_enabled_method_read;
    fields["private_api_manager_enabled_method_rva"] =
        snapshot.manager_enabled_method_rva;
    fields["private_api_names_manager_vtable_read"] =
        snapshot.names_manager_vtable_read;
    fields["private_api_names_manager_vtable_rva"] =
        snapshot.names_manager_vtable_rva;
    fields["private_api_classification_method_read"] =
        snapshot.classification_method_read;
    fields["private_api_classification_method_rva"] =
        snapshot.classification_method_rva;
    fields["private_api_manager_verified"] = snapshot.manager_verified;
    fields["private_api_classification_verified"] =
        snapshot.classification_verified;
    fields["private_api_verified"] = snapshot.verified;
    fields["private_api_manager_initialized_called"] = snapshot.manager_called;
    fields["private_api_manager_initialized"] = snapshot.manager_initialized;
    fields["private_api_manager_initialized_matches_field"] =
        snapshot.manager_called && request.manager_initialized_field_read &&
        snapshot.manager_initialized == request.manager_initialized_field;
    fields["private_api_manager_enabled_called"] = snapshot.manager_called;
    fields["private_api_manager_enabled"] = snapshot.manager_enabled;
    fields["private_api_manager_enabled_matches_field"] =
        snapshot.manager_called && request.manager_enabled_field_read &&
        snapshot.manager_enabled == request.manager_enabled_field;
    fields["private_api_classification_called"] =
        snapshot.classification_called;
    fields["private_api_classification"] = snapshot.classification;
    fields["private_api_classification_matches_field"] =
        snapshot.classification_called && request.classification_field_read &&
        snapshot.classification == request.classification_field;
    fields["private_api_result"] = snapshot.result;
}

} // namespace cxxime_tsf
