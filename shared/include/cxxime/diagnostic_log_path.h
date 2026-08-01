// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_DIAGNOSTIC_LOG_PATH_H_
#define CXXIME_DIAGNOSTIC_LOG_PATH_H_

#include <string>

namespace cxxime {

// Returns a writable diagnostics directory for the current host process.
// Packaged hosts use their package LocalState; other hosts keep the
// historical %USERPROFILE%\cxxime\logs location.
std::wstring diagnostic_log_directory();

} // namespace cxxime

#endif // CXXIME_DIAGNOSTIC_LOG_PATH_H_
