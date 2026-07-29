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

// {70B1D4E3-7834-4D8A-8E7C-9E04E0B36A22}
DEFINE_GUID(c_guidCandidateUIElement, 0x70b1d4e3, 0x7834, 0x4d8a, 0x8e, 0x7c, 0x9e, 0x04, 0xe0, 0xb3, 0x6a, 0x22);

// {8D2F2C68-2CE1-4E85-B3AA-A0907EA5F1E1}
DEFINE_GUID(c_guidReadingUIElement, 0x8d2f2c68, 0x2ce1, 0x4e85, 0xb3, 0xaa, 0xa0, 0x90, 0x7e, 0xa5, 0xf1, 0xe1);

// {F5E6D7C8-B9A0-4123-8765-43210FEDCBA9}
DEFINE_GUID(c_guidPreservedKey_Toggle, 0xf5e6d7c8, 0xb9a0, 0x4123, 0x87, 0x65, 0x43, 0x21, 0x0f, 0xed, 0xcb, 0xa9);

// {C1D2E3F4-A5B6-7890-CDEF-123456789012}
DEFINE_GUID(c_guidLangBarModeButton, 0xc1d2e3f4, 0xa5b6, 0x7890, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12);

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

void DllAddRef();
void DllRelease();

#endif // CXXIME_TSF_GLOBALS_H_
