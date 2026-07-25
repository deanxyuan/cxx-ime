// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_HOST_TAKEOVER_TSF_IMM_FORM_H_
#define CXXIME_HOST_TAKEOVER_TSF_IMM_FORM_H_

#include <windows.h>
#include <imm.h>

#include <cstdint>

namespace cxxime_tsf {

void trace_stage_imm_candidate_form(HWND hwnd,
                                    HIMC himc,
                                    bool before_ok,
                                    const CANDIDATEFORM& before,
                                    bool set_succeeded,
                                    bool after_ok,
                                    const CANDIDATEFORM& after,
                                    uint64_t input_id,
                                    uint64_t composition_id);

void trace_stage_imm_composition_form(HWND hwnd,
                                      HIMC himc,
                                      bool before_ok,
                                      const COMPOSITIONFORM& before,
                                      bool set_succeeded,
                                      bool after_ok,
                                      const COMPOSITIONFORM& after,
                                      uint64_t input_id,
                                      uint64_t composition_id);

} // namespace cxxime_tsf

#endif // CXXIME_HOST_TAKEOVER_TSF_IMM_FORM_H_
