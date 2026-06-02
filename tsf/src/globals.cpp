// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#define INITGUID
#include "globals.h"

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = -1;
CRITICAL_SECTION g_cs;

void DllAddRef() {
    InterlockedIncrement(&g_cRefDll);
}

void DllRelease() {
    InterlockedDecrement(&g_cRefDll);
}

// {2C77A81E-41CC-4178-A3A7-5F8A987568E6}
const GUID GUID_LBI_INPUTMODE = {
    0x2C77A81E, 0x41CC, 0x4178,
    {0xA3, 0xA7, 0x5F, 0x8A, 0x98, 0x75, 0x68, 0xE6}};
