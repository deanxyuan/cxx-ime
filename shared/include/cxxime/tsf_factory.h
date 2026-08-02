// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_FACTORY_H_
#define CXXIME_TSF_FACTORY_H_

#include <msctf.h>

namespace cxxime {

// These helpers call public Msctf.dll exports without requiring a COM apartment.
// Callers with initialized COM should use CoCreateInstance instead.
HRESULT create_tsf_thread_manager_without_com(ITfThreadMgr** thread_manager);
HRESULT create_tsf_category_manager_without_com(ITfCategoryMgr** category_manager);
HRESULT create_tsf_display_attribute_manager_without_com(
    ITfDisplayAttributeMgr** display_attribute_manager);
HRESULT create_tsf_input_processor_profile_manager_without_com(
    ITfInputProcessorProfileMgr** profile_manager);

} // namespace cxxime

#endif // CXXIME_TSF_FACTORY_H_
