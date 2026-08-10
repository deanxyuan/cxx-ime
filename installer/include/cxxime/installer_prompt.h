// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_INSTALLER_PROMPT_H_
#define CXXIME_INSTALLER_PROMPT_H_

#include <cstdint>
#include <string>

#include <cxxime/installer_lock.h>

namespace cxxime {
namespace installer {

enum class LockPromptMode {
    kInstall,
    kUninstall,
};

enum class LockPromptChoice {
    kRetry,
    kDeferUntilRestart,
    kCancel,
    kFailed,
};

LockPromptChoice show_lock_prompt(LockPromptMode mode,
                                  std::uintptr_t parent_window,
                                  const LockQueryResult& result,
                                  const std::wstring& report);

} // namespace installer
} // namespace cxxime

#endif // CXXIME_INSTALLER_PROMPT_H_
