// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_GLOBALS_H_
#define CXXIME_TSF_GLOBALS_H_

#include "pch.h"

// {B7E1E5A2-8F3D-4A9C-B6E7-2C4D8F1A3B5E}
DEFINE_GUID(c_clsidTextService, 0xb7e1e5a2, 0x8f3d, 0x4a9c, 0xb6, 0xe7, 0x2c, 0x4d, 0x8f, 0x1a, 0x3b, 0x5e);

// {D4F2C7A1-9E6B-4D8A-A3F5-1B2C3D4E5F60}
DEFINE_GUID(c_guidProfile, 0xd4f2c7a1, 0x9e6b, 0x4d8a, 0xa3, 0xf5, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60);

// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
DEFINE_GUID(c_guidDisplayAttribute, 0xa1b2c3d4, 0xe5f6, 0x7890, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90);

// {F5E6D7C8-B9A0-4123-8765-43210FEDCBA9}
DEFINE_GUID(c_guidPreservedKey_Toggle, 0xf5e6d7c8, 0xb9a0, 0x4123, 0x87, 0x65, 0x43, 0x21, 0x0f, 0xed, 0xcb, 0xa9);

// {C1D2E3F4-A5B6-7890-CDEF-123456789012}
DEFINE_GUID(c_guidLangBarModeButton, 0xc1d2e3f4, 0xa5b6, 0x7890, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12);

// {D2E3F4A5-B6C7-8901-DEFA-234567890123}
DEFINE_GUID(c_guidLangBarImeButton, 0xd2e3f4a5, 0xb6c7, 0x8901, 0xde, 0xfa, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23);

#define TEXTSERVICE_DESC L"CxxIME"
#define TEXTSERVICE_MODEL L"Apartment"
#define TEXTSERVICE_ICON_INDEX 0

#define TEXTSERVICE_LANGID_HANS  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)

// For Windows 8+
#ifndef TF_IPP_CAPS_IMMERSIVESUPPORT
#define TF_IPP_CAPS_IMMERSIVESUPPORT  0x00010000
#define TF_IPP_CAPS_SYSTRAYSUPPORT    0x00020000
#endif

extern HINSTANCE g_hInst;
extern LONG g_cRefDll;
extern CRITICAL_SECTION g_cs;

extern const GUID GUID_LBI_INPUTMODE;

void DllAddRef();
void DllRelease();

namespace cxxime { class ConfigMonitor; class Config; }
cxxime::ConfigMonitor* get_config_monitor();
cxxime::Config get_config();
void reload_global_config();
void init_config_monitor();
void add_config_monitor_ref();
void release_config_monitor_ref();

#endif // CXXIME_TSF_GLOBALS_H_
