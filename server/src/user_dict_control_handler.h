// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_USER_DICT_CONTROL_HANDLER_H_
#define CXXIME_USER_DICT_CONTROL_HANDLER_H_

#include <string>

class SessionManager;

bool handle_user_dict_control_request(SessionManager& session_manager,
                                      const std::string& request_payload,
                                      std::string* response_payload);

#endif // CXXIME_USER_DICT_CONTROL_HANDLER_H_
