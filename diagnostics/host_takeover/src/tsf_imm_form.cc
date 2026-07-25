// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "tsf_imm_form.h"

#include <cxxime/stage_trace.h>

namespace cxxime_tsf {

void trace_stage_imm_candidate_form(HWND hwnd,
                                    HIMC himc,
                                    bool before_ok,
                                    const CANDIDATEFORM& before,
                                    bool set_succeeded,
                                    bool after_ok,
                                    const CANDIDATEFORM& after,
                                    uint64_t input_id,
                                    uint64_t composition_id) {
    const bool aligned = set_succeeded && after_ok &&
        after.dwStyle == CFS_CANDIDATEPOS &&
        after.ptCurrentPos.x == -1000 &&
        after.ptCurrentPos.y == -1000;
    cxxime::write_stage_trace("tsf", "imm.candidate_form", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"before_ok", before_ok},
        {"before_style", before.dwStyle},
        {"before_x", before.ptCurrentPos.x},
        {"before_y", before.ptCurrentPos.y},
        {"set_succeeded", set_succeeded},
        {"after_ok", after_ok},
        {"after_style", after.dwStyle},
        {"after_x", after.ptCurrentPos.x},
        {"after_y", after.ptCurrentPos.y},
        {"thread_id", GetCurrentThreadId()},
        {"result", aligned ? "aligned" : "failed"},
    });
}

void trace_stage_imm_composition_form(HWND hwnd,
                                      HIMC himc,
                                      bool before_ok,
                                      const COMPOSITIONFORM& before,
                                      bool set_succeeded,
                                      bool after_ok,
                                      const COMPOSITIONFORM& after,
                                      uint64_t input_id,
                                      uint64_t composition_id) {
    const bool aligned = set_succeeded && after_ok &&
        after.dwStyle == CFS_FORCE_POSITION &&
        after.ptCurrentPos.x == -1000 &&
        after.ptCurrentPos.y == -1000;
    cxxime::write_stage_trace("tsf", "imm.composition_form", {
        {"input_id", input_id},
        {"composition_id", composition_id},
        {"hwnd", reinterpret_cast<uintptr_t>(hwnd)},
        {"himc", reinterpret_cast<uintptr_t>(himc)},
        {"before_ok", before_ok},
        {"before_style", before.dwStyle},
        {"before_x", before.ptCurrentPos.x},
        {"before_y", before.ptCurrentPos.y},
        {"before_left", before.rcArea.left},
        {"before_top", before.rcArea.top},
        {"before_right", before.rcArea.right},
        {"before_bottom", before.rcArea.bottom},
        {"set_succeeded", set_succeeded},
        {"after_ok", after_ok},
        {"after_style", after.dwStyle},
        {"after_x", after.ptCurrentPos.x},
        {"after_y", after.ptCurrentPos.y},
        {"after_left", after.rcArea.left},
        {"after_top", after.rcArea.top},
        {"after_right", after.rcArea.right},
        {"after_bottom", after.rcArea.bottom},
        {"thread_id", GetCurrentThreadId()},
        {"result", aligned ? "aligned" : "failed"},
    });
}

} // namespace cxxime_tsf
