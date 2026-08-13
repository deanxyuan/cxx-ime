// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_LOG_WRITER_H_
#define CXXIME_TSF_LOG_WRITER_H_

namespace cxxime_tsf {

void enqueue_tsf_log_line(const char* json, int length);
void set_tsf_log_writer_enabled(bool enabled);
void request_tsf_log_writer_stop();
void shutdown_tsf_log_writer();
bool tsf_log_writer_has_thread();

} // namespace cxxime_tsf

#endif // CXXIME_TSF_LOG_WRITER_H_
