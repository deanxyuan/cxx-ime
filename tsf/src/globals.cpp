// Copyright (c) 2026 CxxIME Contributors. MIT License.

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
