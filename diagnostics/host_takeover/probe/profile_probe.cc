// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "probe_app.h"

#include <ctffunc.h>
#include <imm.h>

#include <cxxime/stage_trace.h>

namespace cxxime_probe {
namespace {

struct CategoryMembership {
    HRESULT enum_hr = E_UNEXPECTED;
    bool registered = false;
};

CategoryMembership category_contains(ITfCategoryMgr* manager,
                                      REFGUID category,
                                      REFGUID item) {
    CategoryMembership membership;
    if (!manager) {
        membership.enum_hr = E_POINTER;
        return membership;
    }

    IEnumGUID* items = nullptr;
    membership.enum_hr = manager->EnumItemsInCategory(category, &items);
    if (FAILED(membership.enum_hr) || !items) {
        return membership;
    }

    GUID value = {};
    ULONG fetched = 0;
    while (items->Next(1, &value, &fetched) == S_OK && fetched == 1) {
        if (IsEqualGUID(value, item)) {
            membership.registered = true;
            break;
        }
    }
    items->Release();
    return membership;
}

} // namespace

void ProbeApp::trace_active_keyboard_profile(const char* trigger) {
    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    const HRESULT manager_hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&profile_manager));

    TF_INPUTPROCESSORPROFILE profile = {};
    HRESULT profile_hr = E_UNEXPECTED;
    if (SUCCEEDED(manager_hr) && profile_manager) {
        profile_hr = profile_manager->GetActiveProfile(
            GUID_TFCAT_TIP_KEYBOARD, &profile);
        profile_manager->Release();
    }

    ITfCategoryMgr* category_manager = nullptr;
    const HRESULT category_manager_hr = CoCreateInstance(
        CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&category_manager));
    CategoryMembership keyboard;
    CategoryMembership ui_element;
    CategoryMembership input_mode;
    CategoryMembership display_attribute;
    if (SUCCEEDED(profile_hr) && category_manager) {
        keyboard = category_contains(
            category_manager, GUID_TFCAT_TIP_KEYBOARD, profile.clsid);
        ui_element = category_contains(
            category_manager, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, profile.clsid);
        input_mode = category_contains(
            category_manager, GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT, profile.clsid);
        display_attribute = category_contains(
            category_manager, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, profile.clsid);
    }
    if (category_manager) {
        category_manager->Release();
    }

    const bool verified = profile_hr == S_OK &&
                          IsEqualGUID(profile.catid, GUID_TFCAT_TIP_KEYBOARD) &&
                          keyboard.registered && ui_element.registered &&
                          input_mode.registered && display_attribute.registered &&
                          (profile.dwCaps & TF_IPP_CAPS_UIELEMENTENABLED) != 0;
    const HKL keyboard_layout = GetKeyboardLayout(0);
    cxxime::write_stage_trace("probe", "probe.active_profile", {
        {"trigger", trigger ? trigger : ""},
        {"manager_hr", static_cast<int64_t>(manager_hr)},
        {"profile_hr", static_cast<int64_t>(profile_hr)},
        {"profile_type", profile.dwProfileType},
        {"langid", profile.langid},
        {"clsid", cxxime::stage_trace_guid(profile.clsid)},
        {"profile_guid", cxxime::stage_trace_guid(profile.guidProfile)},
        {"category", cxxime::stage_trace_guid(profile.catid)},
        {"category_is_keyboard",
         IsEqualGUID(profile.catid, GUID_TFCAT_TIP_KEYBOARD) != FALSE},
        {"profile_hkl", reinterpret_cast<uintptr_t>(profile.hkl)},
        {"profile_hkl_substitute", reinterpret_cast<uintptr_t>(profile.hklSubstitute)},
        {"profile_caps", profile.dwCaps},
        {"profile_caps_ui_element", (profile.dwCaps & TF_IPP_CAPS_UIELEMENTENABLED) != 0},
        {"profile_flags", profile.dwFlags},
        {"category_manager_hr", static_cast<int64_t>(category_manager_hr)},
        {"keyboard_category_hr", static_cast<int64_t>(keyboard.enum_hr)},
        {"keyboard_category_registered", keyboard.registered},
        {"ui_element_category_hr", static_cast<int64_t>(ui_element.enum_hr)},
        {"ui_element_category_registered", ui_element.registered},
        {"input_mode_category_hr", static_cast<int64_t>(input_mode.enum_hr)},
        {"input_mode_category_registered", input_mode.registered},
        {"display_attribute_category_hr", static_cast<int64_t>(display_attribute.enum_hr)},
        {"display_attribute_category_registered", display_attribute.registered},
        {"thread_hkl", reinterpret_cast<uintptr_t>(keyboard_layout)},
        {"thread_hkl_is_ime", ImmIsIME(keyboard_layout) != FALSE},
        {"result", verified ? "verified" : "incomplete"},
    });
}

} // namespace cxxime_probe
