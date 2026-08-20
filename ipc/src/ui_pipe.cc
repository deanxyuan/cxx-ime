// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "ui_pipe.h"

#include <limits>

namespace cxxime {
namespace ui_pipe {

HANDLE connect_pipe(const std::wstring& pipe_name, HANDLE stop_event) {
    while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
        if (!WaitNamedPipeW(pipe_name.c_str(), kConnectRetryMs)) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY &&
                error != ERROR_SEM_TIMEOUT) {
                return INVALID_HANDLE_VALUE;
            }
            if (WaitForSingleObject(stop_event, kConnectRetryMs) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            continue;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        if (SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
            return pipe;
        }
        CloseHandle(pipe);
    }
    return INVALID_HANDLE_VALUE;
}

bool connect_pipe_instance(HANDLE pipe, HANDLE stop_event, const std::atomic<bool>& running) {
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return false;
    }

    bool connected = false;
    BOOL started = ConnectNamedPipe(pipe, &overlapped);
    if (started) {
        connected = true;
    } else {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (error == ERROR_IO_PENDING) {
            HANDLE handles[] = {stop_event, overlapped.hEvent};
            const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) {
                DWORD transferred = 0;
                connected = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
            } else {
                CancelIoEx(pipe, &overlapped);
                DWORD transferred = 0;
                GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
            }
        }
    }

    CloseHandle(overlapped.hEvent);
    return connected && running.load(std::memory_order_acquire);
}

bool write_packet(HANDLE pipe, HANDLE stop_event, const std::vector<std::uint8_t>& packet) {
    if (!pipe || pipe == INVALID_HANDLE_VALUE || packet.empty() ||
        packet.size() > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
        return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        return false;
    }

    DWORD transferred = 0;
    BOOL succeeded =
        WriteFile(pipe, packet.data(), static_cast<DWORD>(packet.size()), nullptr, &overlapped);
    if (succeeded) {
        succeeded = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
    } else if (GetLastError() == ERROR_IO_PENDING) {
        HANDLE handles[] = {stop_event, overlapped.hEvent};
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, kWriteTimeoutMs);
        if (wait == WAIT_OBJECT_0 + 1) {
            succeeded = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
        } else {
            CancelIoEx(pipe, &overlapped);
            GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
            succeeded = FALSE;
        }
    }

    CloseHandle(overlapped.hEvent);
    return succeeded != FALSE && transferred == static_cast<DWORD>(packet.size());
}

} // namespace ui_pipe
} // namespace cxxime
