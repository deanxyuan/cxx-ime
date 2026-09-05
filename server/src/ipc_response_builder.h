// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_SERVER_IPC_RESPONSE_BUILDER_H_
#define CXXIME_SERVER_IPC_RESPONSE_BUILDER_H_

#include <cxxime/ipc_protocol.h>

#include "session_manager.h"

void fill_process_response(const ProcessKeyResult& result, cxxime::IPCResponse* response);

#endif // CXXIME_SERVER_IPC_RESPONSE_BUILDER_H_
