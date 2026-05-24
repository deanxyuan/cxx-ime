// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
//
// Pipe security attributes using SDDL.
// Grants access to SYSTEM, Everyone, and All Application Packages (UWP),
// with Low integrity mandatory label (NO_WRITE_UP) for IE protected mode / UWP compatibility.
// Reference: weasel WeaselIPCServer/SecurityAttribute.cpp

#ifndef CXXIME_SECURITY_ATTRIBUTES_H_
#define CXXIME_SECURITY_ATTRIBUTES_H_

#include <windows.h>
#include <sddl.h>

namespace cxxime {

// SDDL: D:(A;;GA;;;SY)(A;;GA;;;WD)(A;;GA;;;AC)S:(ML;;NW;;;LW)
//   SY = SYSTEM (full access)
//   WD = Everyone (full access)
//   AC = All Application Packages (full access, for UWP)
//   LW = Low integrity mandatory label, NW = No Write Up
constexpr wchar_t PIPE_SDDL[] =
    L"D:"
    L"(A;;GA;;;SY)"
    L"(A;;GA;;;WD)"
    L"(A;;GA;;;AC)"
    L"S:(ML;;NW;;;LW)";

class SecurityAttributes {
public:
    SecurityAttributes() {
        PSECURITY_DESCRIPTOR pSD = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                PIPE_SDDL, SDDL_REVISION_1, &pSD, nullptr)) {
            sa_.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa_.lpSecurityDescriptor = pSD;
            sa_.bInheritHandle = FALSE;
        }
    }

    ~SecurityAttributes() {
        if (sa_.lpSecurityDescriptor) {
            LocalFree(sa_.lpSecurityDescriptor);
        }
    }

    SecurityAttributes(const SecurityAttributes&) = delete;
    SecurityAttributes& operator=(const SecurityAttributes&) = delete;

    SECURITY_ATTRIBUTES* get() {
        return sa_.lpSecurityDescriptor ? &sa_ : nullptr;
    }

    bool valid() const { return sa_.lpSecurityDescriptor != nullptr; }

private:
    SECURITY_ATTRIBUTES sa_ = {};
};

} // namespace cxxime

#endif // CXXIME_SECURITY_ATTRIBUTES_H_
