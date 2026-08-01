// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_CONTEXT_H_
#define CXXIME_HOST_TAKEOVER_TSF_CONTEXT_H_

#include <cstdint>

#include <windows.h>
#include <msctf.h>

#include "edit_target.h"

namespace cxxime_tsf {

void trace_stage_edit_target(uint64_t input_id, uint64_t composition_id, EditTargetState state,
                             const EditTargetEvidence& evidence);
void trace_stage_context_state(uint64_t input_id, uint64_t composition_id, ITfContext* context);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_CONTEXT_H_
