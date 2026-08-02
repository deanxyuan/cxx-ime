// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "util/testutil.h"

#include <windows.h>

#include <cxxime/tsf_factory.h>

namespace {

void verify_com_is_not_initialized() {
    ULONG_PTR context_token = 0;
    ASSERT_EQ(CoGetContextToken(&context_token), CO_E_NOTINITIALIZED);
}

void verify_tsf_factories() {
    ITfThreadMgr* thread_manager = nullptr;
    ASSERT_EQ(cxxime::create_tsf_thread_manager_without_com(&thread_manager), S_OK);
    ASSERT_TRUE(thread_manager != nullptr);
    thread_manager->Release();

    ITfCategoryMgr* category_manager = nullptr;
    ASSERT_EQ(cxxime::create_tsf_category_manager_without_com(&category_manager), S_OK);
    ASSERT_TRUE(category_manager != nullptr);
    category_manager->Release();

    ITfDisplayAttributeMgr* display_attribute_manager = nullptr;
    ASSERT_EQ(cxxime::create_tsf_display_attribute_manager_without_com(
                  &display_attribute_manager),
              S_OK);
    ASSERT_TRUE(display_attribute_manager != nullptr);
    display_attribute_manager->Release();

    ITfInputProcessorProfileMgr* profile_manager = nullptr;
    ASSERT_EQ(cxxime::create_tsf_input_processor_profile_manager_without_com(
                &profile_manager),
              S_OK);
    ASSERT_TRUE(profile_manager != nullptr);
    profile_manager->Release();
}

} // namespace

TEST(TsfFactory, works_without_com_initialization) {
    verify_com_is_not_initialized();
    verify_tsf_factories();
    verify_com_is_not_initialized();
}

TEST(TsfFactory, works_in_mta) {
    const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT_TRUE(SUCCEEDED(initialize_result));
    verify_tsf_factories();
    CoUninitialize();
    verify_com_is_not_initialized();
}

TEST(TsfFactory, rejects_null_output) {
    ASSERT_EQ(cxxime::create_tsf_thread_manager_without_com(nullptr), E_POINTER);
    ASSERT_EQ(cxxime::create_tsf_category_manager_without_com(nullptr), E_POINTER);
    ASSERT_EQ(cxxime::create_tsf_display_attribute_manager_without_com(nullptr), E_POINTER);
    ASSERT_EQ(cxxime::create_tsf_input_processor_profile_manager_without_com(nullptr), E_POINTER);
}

RUN_ALL_TESTS()
