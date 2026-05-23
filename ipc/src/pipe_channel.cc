// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <windows.h>
#include <cxxime/ipc_protocol.h>

namespace cxxime {

bool pipe_write(HANDLE pipe, const void* data, DWORD size) {
    DWORD written = 0;
    return WriteFile(pipe, data, size, &written, nullptr) && written == size;
}

bool pipe_read(HANDLE pipe, void* data, DWORD size) {
    DWORD read_bytes = 0;
    return ReadFile(pipe, data, size, &read_bytes, nullptr) && read_bytes == size;
}

} // namespace cxxime
